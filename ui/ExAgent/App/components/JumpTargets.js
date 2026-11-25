// 仅使用目录与运行标识导航，同名工作树和同名会话通过副标题区分。
function collect(projectRows, activeRows, terminalRows, terminalTabs) {
    const projects = new Map(), paths = new Map(), results = []
    for (const r of projectRows) {
        if (r.kind === 1) projects.set(r.project, r.title)
        else if (r.kind === 2) paths.set(r.workspace, r.path)
    }
    for (const r of activeRows.concat(terminalRows)) {
        if (r.pending) results.push({title:r.title, detail:r.workspace, pending:true, session:r.session,
            run:r.run, token:r.token, terminal:r.terminal, type:"等待处理"})
    }
    for (const r of projectRows) {
        let detail = projects.get(r.project) || ""
        if (r.kind !== 1 && paths.has(r.workspace)) detail += " · " + paths.get(r.workspace)
        results.push({title:r.title, detail:detail, project:r.project, workspace:r.workspace, session:r.session,
            type:["","项目","工作树","会话"][r.kind]})
    }
    for (const t of terminalTabs)
        results.push({title:t.title, detail:t.cwd, terminal:t.id, type:"终端"})
    return results
}
function filter(rows, query) {
    const terms=query.trim().toLowerCase().split(/\s+/).filter(Boolean)
    const first=[], rest=[]
    for (const row of rows) {
        const title=row.title.toLowerCase(), detail=(row.detail || "").toLowerCase()
        if (!terms.every(term => (title+" "+detail+" "+row.type).includes(term))) continue
        const bucket=terms.every(term=>title.includes(term)) ? first : rest
        if (bucket.length<80) bucket.push(row)
    }
    return first.concat(rest).slice(0,80)
}
