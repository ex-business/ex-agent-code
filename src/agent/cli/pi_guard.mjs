import fs from "node:fs";
import path from "node:path";
export default function(pi) {
    pi.on("session_start",(_,ctx)=>{ if(ctx.mode==="rpc")ctx.ui.notify("EX_NATIVE_GUARD_V1","info"); });
    pi.on("tool_call",async(event,ctx)=>{
        try {
        const write=process.env.EX_NATIVE_WRITE==="1",full=process.env.EX_NATIVE_FULL==="1";
        const reads=new Set(["read","grep","find","ls"]);
        if(!write && !reads.has(event.toolName))return {block:true,reason:"当前会话只读，未执行工具"};
        if(reads.has(event.toolName)) {
            try { const base=fs.realpathSync(ctx.cwd),target=fs.realpathSync(path.resolve(ctx.cwd,event.input?.path||"."));
                if(target!==base && !target.startsWith(base+path.sep))return {block:true,reason:"读取路径不在当前工作区"};
            } catch { return {block:true,reason:"无法核对工具路径"}; }
            return;
        }
        if(full)return;
        const allowed=await ctx.ui.confirm("工具审批",event.toolName+"\n"+JSON.stringify(event.input));
        if(!allowed)return {block:true,reason:"用户拒绝此次操作"};
        } catch { return {block:true,reason:"审批异常，未执行工具"}; }
    });
}
