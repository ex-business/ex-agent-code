// 保留最后一次真实上报。进程意外退出时，时间戳可区分旧读数。
import fs from "node:fs";
import path from "node:path";
import {createHash} from "node:crypto";
export function registerActivity(pi, dir, origin) {
    if (!dir || !origin) return;
    const name = "pi-" + createHash("sha256").update(origin).digest("hex") + ".json";
    const target = path.join(dir,name);
    let status = 0, tokens, settled = 3, beforePrompt = 0;
    function publish(ctx) {
        if (ctx.sessionManager.getSessionFile() !== origin) return;
        try {
            fs.mkdirSync(dir,{recursive:true,mode:0o700});
            const tmp = target + "." + process.pid + ".tmp";
            fs.writeFileSync(tmp,JSON.stringify({agent:"Pi",session:ctx.sessionManager.getSessionId(),
                terminalId:path.basename(process.env.EX_SLEEP_DIR || ""),pid:process.pid,
                status,tokens,updated:Date.now()}),{mode:0o600});
            fs.renameSync(tmp,target);
        } catch { /* 诊断快照写入失败不能中断 Agent 回合。 */ }
    }
    for (const [event,state] of [["session_start",0],["agent_start",1],["ui_prompt_start",2],
        ["ui_prompt_end",1],["agent_settled",3],["session_shutdown",5]]) {
        pi.on(event,(_,ctx)=>{
            if (event === "agent_start") settled = 3;
            if (event === "ui_prompt_start") beforePrompt = status;
            status = event === "ui_prompt_end" ? beforePrompt : event === "agent_settled" ? settled : state;
            publish(ctx);
        });
    }
    pi.on("message_end",({message},ctx)=>{
        if (message?.role !== "assistant") return;
        if (message.stopReason === "error") settled = 4;
        if (message.stopReason === "aborted") settled = 5;
        const n = message.usage?.totalTokens;
        tokens = Number.isSafeInteger(n) && n>=0 ? n : undefined;
        publish(ctx);
    });
}
