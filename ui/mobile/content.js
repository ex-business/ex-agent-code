'use strict';
// 长正文和图片按用户请求异步加载，缓存与会话授权代次绑定，离开窗口即释放图像对象URL。
import {t} from './appearance.js';
let generation=0,operations=0;
const images=new Map(),textCache=new Map();
let imageBytes=0;
function releaseImage(v){for(const [node,button] of v.nodes){node.removeAttribute('src');node.hidden=true;button.textContent=t('查看图片');}v.nodes.clear();URL.revokeObjectURL(v.url);}
export function clearContent(){generation++;for(const v of images.values())releaseImage(v);images.clear();textCache.clear();imageBytes=0;}
function active(epoch,article,message,stamp){return epoch===generation&&article.isConnected&&article.dataset.revision===message.revision&&article.dataset.contentEpoch===stamp;}
function pruneText(){while(textCache.size>8)textCache.delete(textCache.keys().next().value);}
function fail(text){throw new Error(t(text));}
export function extraContent(article,message,api,renderText){
 article.dataset.revision=message.revision;
 const epoch=generation,stamp=String((Number(article.dataset.contentEpoch)||0)+1);article.dataset.contentEpoch=stamp;
 if(message.truncated===1){
  const key=message.id+':'+message.revision;
  let loaded=textCache.get(key)||message.text;
  const body=article.querySelector('.message-body');
  if(loaded.length>message.text.length){body.replaceChildren();renderText(body,loaded);}
  const more=document.createElement('button');more.className='quiet content-action';more.textContent=t('加载更多正文');more.hidden=loaded.length>=message.length;
  const hint=document.createElement('small');hint.className='content-hint';
  const count=()=>{hint.textContent=loaded.length+' / '+message.length;};count();
  more.onclick=async()=>{
   if(operations>=2)return;operations++;
   more.disabled=true;
   try{
    // 每次最多补充64K，避免把超长正文一次排入DOM；完整文本可另存下载。
    for(let n=0;n<4&&loaded.length<message.length&&loaded.length<1048576;++n){
     const r=(await api('message',{message:message.id,revision:message.revision,offset:loaded.length})).result;
     if(!active(epoch,article,message,stamp))return;
     if(r.message!==message.id||r.revision!==message.revision||r.offset!==loaded.length||r.total!==message.length||typeof r.text!=='string'||r.text.length>16384||!r.text.length||r.offset+r.text.length>r.total||(r.next!==null&&r.next!==r.offset+r.text.length)||(r.next===null&&r.offset+r.text.length!==r.total))fail('正文分段校验失败');
     loaded+=r.text;
    }
    if(loaded.length<=1048576){textCache.set(key,loaded);pruneText();}
    body.replaceChildren();renderText(body,loaded);more.hidden=loaded.length>=message.length;count();if(loaded.length>=1048576)more.textContent=t('更长正文请下载');
   }catch(e){if(active(epoch,article,message,stamp))hint.textContent=e.message;}
   finally{operations--;more.disabled=loaded.length>=1048576;}
  };
  // DOM正文保持有界，较大消息通过下载获取全量原始文本。
  const download=document.createElement('button');download.className='quiet content-action';download.textContent=t('下载完整文本');
  download.onclick=async()=>{
   if(operations>=2)return;operations++;
   download.disabled=true;
   try{
    if(message.length>16*1024*1024)fail('正文超过下载容量，请在桌面导出');
    const chunks=[];let offset=0;
    while(offset<message.length){
     const r=(await api('message',{message:message.id,revision:message.revision,offset})).result;
     if(!active(epoch,article,message,stamp))return;
     if(r.message!==message.id||r.revision!==message.revision||r.offset!==offset||r.total!==message.length||typeof r.text!=='string'||r.text.length===0||r.text.length>16384||offset+r.text.length>r.total||(r.next!==null&&r.next!==offset+r.text.length)||(r.next===null&&offset+r.text.length!==r.total))fail('正文分段校验失败');
     chunks.push(r.text);offset+=r.text.length;
    }
    const url=URL.createObjectURL(new Blob(chunks,{type:'text/plain;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download='message.txt';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
   }catch(e){if(active(epoch,article,message,stamp))hint.textContent=e.message;}
   finally{operations--;download.disabled=false;}
  };
  more.disabled=loaded.length>=1048576;if(more.disabled)more.textContent=t('更长正文请下载');
  article.append(more,download,hint);
 }
 for(const attachment of message.attachments||[]){
  const card=document.createElement('figure');card.className='shared-image';
  const label=document.createElement('figcaption');label.textContent=attachment.name||t('图片附件');
  const button=document.createElement('button');button.className='quiet';button.textContent=t('查看图片');
  const image=document.createElement('img');image.alt=attachment.name||t('图片附件');image.hidden=true;image.loading='lazy';image.decoding='async';
  const hint=document.createElement('small');
  const key=message.id+':'+message.revision+':'+attachment.index;
  const show=entry=>{for(const [node] of entry.nodes)if(!node.isConnected)entry.nodes.delete(node);entry.nodes.set(image,button);image.src=entry.url;image.hidden=false;button.textContent=t('收起图片');};
  button.onclick=async()=>{
   if(!image.hidden){image.removeAttribute('src');image.hidden=true;images.get(key)?.nodes.delete(image);button.textContent=t('查看图片');return;}
   if(images.has(key)){show(images.get(key));return;}
   if(operations>=2)return;operations++;
   button.disabled=true;hint.textContent=t('正在加载图片');
   try{
    let offset=0,version='',total=0,type='';const deadline=Date.now()+30000,chunks=[];
    do{
     if(Date.now()>=deadline)fail('图片准备超时，请重试');
     const r=(await api('image',{message:message.id,revision:message.revision,index:attachment.index,offset,...(version?{version}:{})},true,Math.max(1,Math.min(10000,deadline-Date.now())))).result;
     if(!active(epoch,article,message,stamp))return;
     if(r.state===0){await new Promise(resolve=>setTimeout(resolve,100));continue;}
     if(r.state!==1)fail('图片分段校验失败');
     if(r.message!==message.id||r.index!==attachment.index||r.offset!==offset||!Number.isInteger(r.total)||r.total<=0||r.total>20*1024*1024||!['image/png','image/jpeg','image/webp','image/gif'].includes(r.type)||typeof r.version!=='string'||(version&&version!==r.version)||(total&&total!==r.total)||(type&&type!==r.type)||typeof r.data!=='string'||r.data.length>175000)fail('图片分段校验失败');
     version=r.version;total=r.total;type=r.type;
     const data=Uint8Array.from(atob(r.data),c=>c.charCodeAt(0));if(!data.length||offset+data.length>total)fail('图片分段校验失败');chunks.push(data);offset+=data.length;
     if((r.next===null&&offset!==total)||(r.next!==null&&r.next!==offset))fail('图片分段校验失败');
    }while(total===0||offset<total);
    while(images.size>=3||imageBytes+total>32*1024*1024){const first=images.keys().next().value;if(first===undefined)break;const old=images.get(first);releaseImage(old);imageBytes-=old.size;images.delete(first);}
    if(images.has(key)){const old=images.get(key);releaseImage(old);imageBytes-=old.size;images.delete(key);}
    const entry={url:URL.createObjectURL(new Blob(chunks,{type})),size:total,nodes:new Map()};images.set(key,entry);imageBytes+=total;show(entry);hint.textContent='';
   }catch(e){if(active(epoch,article,message,stamp))hint.textContent=e.message;}
   finally{operations--;button.disabled=false;}
  };
  card.append(label,button,image,hint);article.append(card);
 }
}
