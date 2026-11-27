'use strict';
const english = {
 '移动会话':'Mobile chat','连接桌面的会话':'Connect to a desktop chat','断开':'Disconnect','连接你的桌面':'Connect to your desktop',
 '在桌面「移动接入」中为当前会话生成配对链接。':'Create a pairing link for this chat in Mobile Access on your desktop.',
 '设备名称':'Device name','我的手机':'My phone','配对码':'Pairing code','配对':'Pair','加载更早消息':'Load earlier messages','回到最新':'Back to latest',
 '消息':'Message','发送到桌面会话…':'Send to your desktop chat…','发送':'Send','已在桌面核对，解锁输入':'Checked on desktop — unlock input',
 '请求未成功':'Request failed','连接已变化':'Connection changed','连接已失效，请在桌面重新配对':'Access ended. Pair again from desktop.',
 '核对发送':'Check submission','桌面正在运行':'Desktop is running','允许发送 · 沿用桌面权限':'Can send · desktop permissions apply','只读访问':'Read-only',
 '你':'You','助手':'Assistant','工具':'Tool','执行记录':'Activity','进行中':'Running','失败':'Failed',
 '长消息显示前16K字符，完整内容请在桌面查看。':'Showing the first 16K characters. Read the full message on desktop.',
 '正在阅读历史，点击“回到最新”恢复实时更新':'Reading history. Back to latest resumes live updates.','还没有消息':'No messages yet',
 '已连接桌面':'Connected to desktop','正在重连':'Reconnecting','已提交桌面执行':'Submitted to desktop',
 '桌面未接受输入，草稿已保留':'Desktop did not accept the input. Draft kept.',
 '发送结果待核对，请保留请求ID并在桌面确认':'Submission needs review. Keep its request ID and check on desktop.',
 '保留原请求，点击核对发送可安全重试':'Request kept. Check submission to safely retry the same request.',
 '已按你的确认解锁，原发送记录仍保留在桌面':'Input unlocked as confirmed. The original submission record remains on desktop.'
};
let language='zh-CN', strings={};
export function t(source) {return strings[source] || (language.startsWith('en') ? english[source] : '') || source;}
export async function syncAppearance() {
 const response=await fetch('/appearance.json',{cache:'no-store',credentials:'omit'});if(!response.ok)return;
 applyAppearance(await response.json());
}
export function applyAppearance(value) {
 language=value.language||'zh-CN';strings=value.strings||{};document.documentElement.lang=language;
 const colors={background:'--background',text:'--text',surface:'--surface',border:'--line',secondary:'--muted',code:'--user',accent:'--button',onAccent:'--inverse'};
 for(const [key,variable] of Object.entries(colors)){const color=value.colors?.[key];if(typeof color==='string'&&CSS.supports('color',color))document.documentElement.style.setProperty(variable,color);}
 for(const node of document.querySelectorAll('[data-i18n]'))node.textContent=t(node.dataset.i18n);
 for(const node of document.querySelectorAll('[data-i18n-placeholder]'))node.placeholder=t(node.dataset.i18nPlaceholder);
}
