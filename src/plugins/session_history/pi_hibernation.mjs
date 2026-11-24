// Pi 在自己的事件循环内确认回合结束；宿主不能凭终端画面推断模型已经停止。
import {execFileSync} from "node:child_process";
import fs from "node:fs";

import {registerActivity} from "./pi_activity.mjs";

export default function register(pi)
{
    const { EX_SLEEP_DIR : dir, EX_SLEEP_ORIGIN : origin, TMUX, TMUX_PANE : pane } = process.env;
    registerActivity(pi,process.env.EX_ACTIVITY_DIR,origin);
    if (!dir || !origin || !TMUX || !pane)
        return;
    const lease = dir + "/permit";
    let timer, unsubscribe, done = false, waiting = false, requested = false, idleSince = Date.now();
    const tmux = (...args) => execFileSync("tmux", [ "-S", TMUX.split(",")[0], ...args ], {
        encoding : "utf8",
        timeout : 1500,
        maxBuffer : 4 * 1024 * 1024,
        stdio : [ "ignore", "pipe", "pipe" ]
    });
    function check(ctx)
    {
        if (requested)
            return;
        try {
            const now = Date.now();
            const policy = JSON.parse(fs.readFileSync(lease, "utf8"));
            if (policy.validUntil < now || policy.validUntil > now + 20000 || !done || waiting
                || ctx.sessionManager.getSessionFile() !== origin || !ctx.isIdle() || ctx.hasPendingMessages()
                || ctx.ui.getEditorText().length) {
                idleSince = now;
                return;
            }
            const delay = Math.max(1, Math.min(1440, policy.minutes)) * 60000;
            if (!(now - Math.max(idleSince, policy.since) >= delay))
                return;
            // 附接的其他客户端、额外窗格及仍存活的子进程都拥有终端，保留运行。
            const state = tmux("display-message", "-p", "-t", pane,
                "#{session_attached} #{session_windows} #{window_panes} #{window_activity}")
                              .trim()
                              .split(" ");
            if (state.length !== 4 || state.slice(0, 3).join(" ") !== "0 1 1"
                || !(now - Number(state[3]) * 1000 >= delay)) {
                idleSince = now;
                return;
            }
            const processes
                = execFileSync("/bin/ps", [ "-axo", "ppid=,comm=" ], { encoding : "utf8", timeout : 1500 });
            if (processes.split("\n").some(line => {
                    const m = line.trim().match(/^(\d+)\s+(.+)$/);
                    return m && Number(m[1]) === process.pid && !/^(\/bin\/)?ps$/.test(m[2]);
                })) {
                idleSince = now;
                return;
            }
            const frame = tmux("capture-pane", "-p", "-t", pane, "-S", "-1500").slice(-512 * 1024);
            const latest = fs.readFileSync(lease, "utf8");
            const fresh = JSON.parse(latest);
            if (fresh.since !== policy.since || fresh.validUntil < Date.now())
                return;
            fs.writeFileSync(dir + "/frame.tmp", frame, { mode : 0o600 });
            fs.renameSync(dir + "/frame.tmp", dir + "/frame");
            fs.writeFileSync(dir + "/sleep.tmp",
                JSON.stringify({ origin, session : ctx.sessionManager.getSessionId(), pid : process.pid }),
                { mode : 0o600 });
            fs.renameSync(dir + "/sleep.tmp", dir + "/sleep");
            requested = true;
            ctx.shutdown();
        } catch {
            idleSince = Date.now();
        } // 无法验证活动或保存画面时保持运行。
    }
    pi.on("session_start", (_event, ctx) => {
        const last = ctx.sessionManager.getBranch().filter(e => e.type === "message").at(-1)?.message;
        done = last?.role === "assistant" && last.stopReason === "stop";
        idleSince = Date.now();
        unsubscribe = ctx.ui.onTerminalInput?.(() => { idleSince = Date.now(); });
        timer = setInterval(() => check(ctx), 2500);
        timer.unref();
    });
    pi.on("ui_prompt_start", () => {
        waiting = true;
        idleSince = Date.now();
    });
    pi.on("ui_prompt_end", () => {
        waiting = false;
        idleSince = Date.now();
    });
    pi.on("agent_start", () => { done = false; });
    pi.on("agent_settled", () => {
        done = true;
        idleSince = Date.now();
    });
    pi.on("session_shutdown", () => {
        clearInterval(timer);
        unsubscribe?.();
    });
}
