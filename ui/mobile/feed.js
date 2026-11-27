'use strict';
// 协议归约与DOM分离。只有整页校验完成后才发布新游标，网络失败可从原位置重试。
function invalid() {const error=new Error('共享内容需要重新同步');error.resetFeed=true;throw error;}
function validRow(row) {
 return row && typeof row.id==='string' && row.id.length>0 && row.id.length<=4096 &&
  Number.isInteger(row.position) && row.position>=0 && Number.isInteger(row.role) && row.role>=1 && row.role<=4 &&
  Number.isInteger(row.status) && row.status>=0 && row.status<=3 && typeof row.text==='string' && row.text.length<=16384 &&
  typeof row.revision==='string' && /^[a-f0-9]{64}$/.test(row.revision) && Number.isInteger(row.length) && row.length>=row.text.length &&
  Array.isArray(row.attachments) && row.attachments.length<=16 && row.attachments.every(a=>Number.isInteger(a.index)&&a.index>=0&&a.index<16&&typeof a.name==='string'&&a.name.length<=160) &&
  typeof row.time==='string' && row.time.length<=128 && (row.truncated===0 || row.truncated===1);
}
export function projectFeed(previous,cursor,frame) {
 const next=frame?.cursor;
 if(!next || typeof next.epoch!=='string' || next.epoch.length>64 || !next.epoch || !Number.isInteger(next.after) || next.after<0 || next.after>1000000000 || (frame.more!==0 && frame.more!==1))invalid();
 let rows,older;
 if(frame.mode===1) {
  if(!Array.isArray(frame.items) || frame.items.length>30 || !frame.items.every(validRow))invalid();
  rows=new Map(frame.items.map(row=>[row.id,row]));if(rows.size!==frame.items.length)invalid();older=frame.older||'';
 } else if(frame.mode===2) {
  if(!cursor || next.epoch!==cursor.epoch || next.after<cursor.after || !Array.isArray(frame.events) || frame.events.length>32)invalid();
  rows=new Map(previous);let last=cursor.after;
  const first=rows.size?Math.min(...[...rows.values()].map(row=>row.position)):0;
  for(const event of frame.events) {
   if(event.kind!==1 || !Number.isInteger(event.seq) || event.seq<=last || event.seq>next.after || !validRow(event.row))invalid();
   last=event.seq;
   // 不把尚未加载的旧消息补丁插入到当前窗口；向前分页时会取得它的最新内容。
   if(rows.has(event.row.id) || event.row.position>=first)rows.set(event.row.id,event.row);
  }
 } else invalid();
 rows=new Map([...rows].sort((a,b)=>a[1].position-b[1].position));
 if(rows.size>200){rows=new Map([...rows].slice(-200));older=rows.keys().next().value;}
 return {rows,cursor:next,older,changed:frame.mode===1 || frame.events.length>0};
}
