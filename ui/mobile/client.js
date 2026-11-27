'use strict';
import {t,syncAppearance,applyAppearance} from './appearance.js';
const $ = id => document.getElementById(id);
let token = localStorage.getItem('exMobileToken') || '', older = '', running = 0, scope = 1, timer, backoff = 2000, pending = null;
let deviceId=localStorage.getItem('exMobileDevice')||'', boundSession=localStorage.getItem('exMobileSession')||'';
const pendingKey=()=> 'exMobilePending:'+deviceId;
const draftKey=()=> 'exMobileDraft:'+boundSession;
function activateSession(id) {
 if(boundSession!==id) {
  if(boundSession)localStorage.setItem(draftKey(),$('draft').value);
  boundSession=id;localStorage.setItem('exMobileSession',id);
  $('draft').value=localStorage.getItem(draftKey())||'';messages.clear();older='';windowEpoch++;
 }
}
let sharing=null, pairingOffer=false;
let messages = new Map(), authEpoch = 0, sending = false, windowEpoch = 0, browsing = false;
try { pending = JSON.parse(localStorage.getItem(pendingKey())); } catch (_) { pending = null; }
$('draft').value = localStorage.getItem(draftKey()) || '';
function receiveOffer() {
 if(location.hash.length<=1)return false;
 pairingOffer=true; $('code').value=location.hash.slice(1);history.replaceState(null,'','/');
 authEpoch++;windowEpoch++;clearTimeout(timer);connected(false);return true;
}
const offered=receiveOffer();
addEventListener('hashchange',()=>receiveOffer());
function status(text) { $('status').textContent = text; }
function connected(value) { $('pairing').hidden = value; $('conversation').hidden = !value; $('composer').hidden = !value; $('disconnect').hidden = !value; }
async function api(path, body, authenticated = true) {
  const epoch = authEpoch;
  const response = await fetch('/api/' + path, {method:'POST', cache:'no-store', credentials:'omit', headers:{'Content-Type':'application/json', ...(authenticated ? {Authorization:'Bearer ' + token} : {})}, body:JSON.stringify(body), signal:AbortSignal.timeout(10000)});
  const value = await response.json();
  if (authenticated && epoch !== authEpoch) throw new Error(t('连接已变化'));
  if (!value.ok) { if (response.status === 401 && authenticated) logout(); const error=new Error(value.error || t('请求未成功'));error.code=value.code;throw error; }
  return value;
}
function logout() { authEpoch++; windowEpoch++; browsing=false; older=""; clearTimeout(timer); token = ''; messages.clear(); $('messages').replaceChildren(); localStorage.removeItem('exMobileToken'); connected(false); status(t('连接已失效，请在桌面重新配对')); }
function controls() { $('control').hidden=!sharing || scope!==2; $('control').textContent=t(sharing?.canSend===1?'交还控制权':'申请控制权'); $('draft').readOnly = !!pending || sending || (scope !== 2 || (sharing && sharing.canSend !== 1)); $('send').disabled = sending || scope !== 2 || (!pending && ((sharing && sharing.canSend !== 1) || running === 1 || !$('draft').value.trim())); $('send').textContent = pending ? t('核对发送') : t('发送'); $('latest').hidden=!browsing; $('scope').textContent = scope === 2 ? (running === 1 ? t('桌面正在运行') : t('允许发送 · 沿用桌面权限')) : t('只读访问'); if(sharing && scope===2)$('scope').textContent=t(sharing.canSend===1?'你正在控制 · 桌面可随时收回':'共享观看 · 发送需桌面交接'); }
function content(article, text) {
  const parts = text.split(/```[^\n]*\n/);
  if (parts.length === 1) { const p = document.createElement('p'); p.textContent = text; article.append(p); return; }
  // 只构造文本节点，消息中的HTML、脚本及链接不能获得页面执行权限。
  let code = false;
  for (const part of text.split(/(```[^\n]*\n?|```)/g)) {
    if (part.startsWith('```')) { code = !code; continue; }
    const node = document.createElement(code ? 'pre' : 'p'); node.textContent = part; article.append(node);
  }
}
function render() {
  const list = $('messages'); const atBottom = window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 100;
  const visible = [...list.children].find(node=>node.getBoundingClientRect().bottom>100);
  const anchor = visible ? {id:visible.dataset.id,top:visible.getBoundingClientRect().top} : null;
  const existing = new Map([...list.children].map(node=>[node.dataset.id,node]));
  let index=0;
  for (const message of messages.values()) {
    let article=existing.get(message.id);
    const signature=document.documentElement.lang+JSON.stringify(message);
    if (!article) {article=document.createElement('article');article.dataset.id=message.id;}
    if (article.dataset.signature!==signature) {
      article.dataset.signature=signature;article.className=message.role===1?'user':'assistant';article.replaceChildren();
      const meta=document.createElement('div');meta.className='meta';meta.textContent=['','你','助手','工具','执行记录'].map(t)[message.role]+(message.status===1?' · '+t('进行中'):message.status===2?' · '+t('失败'):'');article.append(meta);content(article,message.text);
      if (message.truncated===1) {const hint=document.createElement('small');hint.textContent=t('长消息显示前16K字符，完整内容请在桌面查看。');article.append(hint);}
    }
    if (list.children[index]!==article) list.insertBefore(article,list.children[index]||null);
    existing.delete(message.id);index++;
  }
  for (const node of existing.values()) node.remove();
  if (atBottom) window.scrollTo(0,document.documentElement.scrollHeight);
  else if (anchor) {const node=[...list.children].find(x=>x.dataset.id===anchor.id);if(node)window.scrollBy(0,node.getBoundingClientRect().top-anchor.top);}
}
async function snapshot(before = '') {
  const epoch=windowEpoch;
  const value = (await api('snapshot', before ? {before} : {})).result;
  if(epoch!==windowEpoch)return;
  if(value.appearance)applyAppearance(value.appearance);
  sharing=value.sharing||null;
  activateSession(value.session);
  $('title').textContent = value.title || t('移动会话'); running = value.running; scope = value.scope;
  if (before) {
    messages=new Map([...value.items.map(x=>[x.id,x]),...messages]);older=value.older||'';
    if(messages.size>200)messages=new Map([...messages].slice(0,200));
  } else {
    const last=[...messages.keys()].at(-1);
    // 重连后的新快照与缓存不连续时，明确换成连续最新窗口，游标始终指向该窗口的上一页。
    if(last && !value.items.some(x=>x.id===last)) {messages.clear();older=value.older||'';}
    if(messages.size===0)older=value.older||'';
    for(const x of value.items)messages.set(x.id,x);
    if(messages.size>200){messages=new Map([...messages].slice(-200));older=[...messages.keys()][0];}
  }
  $('older').hidden = !older; $('hint').textContent = browsing ? t('正在阅读历史，点击“回到最新”恢复实时更新') : messages.size ? '' : t('还没有消息');
  connected(true);render();controls();
}
async function poll() {
  if (!token || browsing || pairingOffer || document.hidden) return;
  const epoch=authEpoch;
  try { await snapshot(); backoff = 2000; status(t('已连接桌面')); } catch (e) { if(epoch!==authEpoch)return;status(e.message + ' · '+t('正在重连')); backoff=Math.min(backoff*2,30000); }
  if (token && epoch===authEpoch && !browsing && !document.hidden) {clearTimeout(timer);timer=setTimeout(poll,backoff);}
}
$('pair').onclick = async () => {
  const epoch=authEpoch; $('pair').disabled=true;
  try { const value=await api('pair',{code:$('code').value.trim(),name:$('name').value.trim()},false); if(epoch!==authEpoch)return; authEpoch++;clearTimeout(timer);deviceId=value.device.id;localStorage.setItem('exMobileDevice',deviceId);activateSession(value.device.session);token=value.token;localStorage.setItem('exMobileToken',token);pending=null;pairingOffer=false;browsing=false;windowEpoch++;messages.clear();older='';$('code').value='';await poll(); }
  catch(e) {status(e.message);} finally {$('pair').disabled=false;}
};
$('older').onclick = async () => { browsing=true;windowEpoch++;controls();$('older').disabled=true;try {await snapshot(older);}catch(e){status(e.message);}finally{$('older').disabled=false;} };
$('latest').onclick = () => {windowEpoch++;browsing=false;messages.clear();older='';clearTimeout(timer);poll();};
$('draft').oninput = () => {localStorage.setItem(draftKey(),$('draft').value);controls();};
$('send').onclick = async () => {
  if(sending)return; sending=true; const epoch=authEpoch;
  if (!pending) { pending={id:crypto.randomUUID(),prompt:$('draft').value};localStorage.setItem(pendingKey(),JSON.stringify(pending)); }
  $('send').disabled=true;controls();
  try {
    const result=(await api('send',pending)).result;
    if (result.state === 1) {pending=null;localStorage.removeItem(pendingKey());$('draft').value='';localStorage.removeItem(draftKey());status(t('已提交桌面执行'));if(!browsing)await snapshot();}
    else if (result.state === 3) {pending=null;localStorage.removeItem(pendingKey());status(result.error || t('桌面未接受输入，草稿已保留'));}
    else {$('resolve').hidden=false;status(t('发送结果待核对，请保留请求ID并在桌面确认'));}
  } catch(e) { if(epoch!==authEpoch)return; if(e.code===1){pending=null;localStorage.removeItem(pendingKey());await snapshot().catch(()=>{});status(e.message);}else status(e.message + ' · '+t('保留原请求，点击核对发送可安全重试')); }
  finally { sending=false; controls(); }
};
$('resolve').onclick = () => {pending=null;localStorage.removeItem(pendingKey());$('resolve').hidden=true;controls();status(t('已按你的确认解锁，原发送记录仍保留在桌面'));};
$('control').onclick = async () => {
  const epoch=authEpoch;
  $('control').disabled=true;
  try {
    const release=sharing?.canSend===1;
    await api('control',release?{action:2,epoch:sharing.epoch,revision:sharing.revision}:{action:1});
    if(epoch!==authEpoch)return;
    status(t(release?'已交还桌面控制':'已申请，请桌面确认'));
    await snapshot();
  } catch(e) {
    if(epoch!==authEpoch)return;
    await snapshot().catch(()=>{});status(e.message);
  } finally {$('control').disabled=false;}
};
addEventListener('visibilitychange',()=>{clearTimeout(timer);if(!document.hidden)poll();});
$('disconnect').onclick = logout;
syncAppearance().catch(()=>{}).finally(()=>{controls();if(token&&!offered)poll();});
