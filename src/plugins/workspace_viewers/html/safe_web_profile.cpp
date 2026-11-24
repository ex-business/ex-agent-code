#include "safe_web_profile.h"
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QQuickWebEngineDownloadRequest>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QMimeDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QUrlQuery>
namespace exagent {
void registerWorkspaceWebScheme() {
    QWebEngineUrlScheme scheme("ex-view");scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme|QWebEngineUrlScheme::LocalScheme|QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}
namespace {
class Interceptor final : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const auto scheme=info.requestUrl().scheme();
        // 配置不跨线程读取；每个离线 profile 再由 handler 核验随机 origin 和根目录。
        if(scheme!="ex-view" && scheme!="data" && info.requestUrl()!=QUrl("about:blank")) info.block(true);
    }
};
QByteArray jsonString(const QString &source) {
    auto bytes=QJsonDocument(QJsonArray{source}).toJson(QJsonDocument::Compact);bytes=bytes.mid(1,bytes.size()-2);
    return bytes.replace("<","\\u003c").replace(QByteArray::fromHex("e280a8"),"\\u2028").replace(QByteArray::fromHex("e280a9"),"\\u2029");
}
}
class SafeWebProfile::Handler final : public QWebEngineUrlSchemeHandler {
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    QString root,path,host;QByteArray source;bool mermaid=false,dark=false;
    void requestStarted(QWebEngineUrlRequestJob *job) override {
        const auto url=job->requestUrl();
        if(job->requestMethod()!="GET" || url.host()!=host) {job->fail(QWebEngineUrlRequestJob::RequestDenied);return;}
        QByteArray bytes,mime; const QString nonce=QUuid::createUuid().toString(QUuid::Id128);
        QByteArray csp="default-src 'none'; img-src ex-view: data:; style-src 'unsafe-inline' ex-view:; font-src ex-view: data:; connect-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; ";
        if(mermaid) csp+="script-src 'nonce-"+nonce.toLatin1()+"';"; else csp+="script-src 'none';";
        if(mermaid && url.path()=="/__ex/mermaid.js") {
            QFile asset(":/workspace-viewers/mermaid.js");if(asset.open(QIODevice::ReadOnly)) bytes=asset.readAll();mime="application/javascript";
        } else if(url.path()==path) {
            mime="text/html";
            if(!mermaid) bytes=source;
            else bytes="<!doctype html><meta charset='utf-8'><style>body{margin:20px;color:"+QByteArray(dark ? "#e8eaed" : "#34363b")+";background:"+QByteArray(dark ? "#202124" : "#fff")+";font:14px system-ui}svg{max-width:100%;height:auto}pre{white-space:pre-wrap;color:#b42318}</style><div id='diagram'></div><pre id='error'></pre><script nonce='"+nonce.toLatin1()+"' src='/__ex/mermaid.js'></script><script nonce='"+nonce.toLatin1()+"'>const m=ExMermaid.default;m.initialize({startOnLoad:false,theme:'"+QByteArray(dark ? "dark" : "default")+"',securityLevel:'strict',maxTextSize:200000,secure:['securityLevel','startOnLoad','maxTextSize','flowchart'],flowchart:{htmlLabels:false}});m.render('exDiagram',"+jsonString(QString::fromUtf8(source))+").then(x=>document.getElementById('diagram').innerHTML=x.svg).catch(e=>{document.getElementById('error').textContent=e.message;document.getElementById('diagram').replaceChildren()});</script>";
        } else if(!mermaid) {
            const auto local=QFileInfo(QDir(root).absoluteFilePath(url.path().mid(1))).canonicalFilePath();
            if(local.isEmpty() || !local.startsWith(root.endsWith('/') ? root : root+'/')) {job->fail(QWebEngineUrlRequestJob::RequestDenied);return;}
            QFile file(local);if(file.size()>16*1024*1024 || !file.open(QIODevice::ReadOnly)) {job->fail(QWebEngineUrlRequestJob::UrlNotFound);return;}
            mime=QMimeDatabase().mimeTypeForFile(local).name().toLatin1();
            const bool allowed=mime.startsWith("image/") || mime.startsWith("font/") || mime=="text/css" || mime=="text/html" || mime=="application/font-woff";
            if(!allowed || mime=="image/svg+xml") {job->fail(QWebEngineUrlRequestJob::RequestDenied);return;}
            bytes=file.read(16*1024*1024+1);if(file.error()!=QFile::NoError || bytes.size()>16*1024*1024) {job->fail(QWebEngineUrlRequestJob::RequestDenied);return;}
        } else {job->fail(QWebEngineUrlRequestJob::RequestDenied);return;}
        job->setAdditionalResponseHeaders({{"Content-Security-Policy",csp},{"X-Content-Type-Options","nosniff"}});
        // 文档缓冲采用 UTF-8；显式声明编码，避免无 meta 的中文 HTML 被当成西文编码。
        if(mime.startsWith("text/") || mime=="application/javascript") mime+="; charset=utf-8";
        auto *buffer=new QBuffer(job);buffer->setData(bytes);buffer->open(QIODevice::ReadOnly);job->reply(mime,buffer);
    }
};
SafeWebProfile::SafeWebProfile(QObject *parent):QQuickWebEngineProfile(parent),m_handler(new Handler(this)) {
    setOffTheRecord(true);setHttpCacheType(QQuickWebEngineProfile::NoCache);setPersistentCookiesPolicy(QQuickWebEngineProfile::NoPersistentCookies);
    setUrlRequestInterceptor(new Interceptor(this));installUrlSchemeHandler("ex-view",m_handler);
    connect(this,&QQuickWebEngineProfile::downloadRequested,this,[](QQuickWebEngineDownloadRequest *request){request->cancel();});
}
void SafeWebProfile::configure(const QString &root,const QString &path,const QString &source,bool mermaid,bool dark) {
    const auto base=QFileInfo(root).canonicalFilePath(),canonical=QFileInfo(path).canonicalFilePath();
    if(base.isEmpty() || base!=root || canonical.isEmpty() || !canonical.startsWith(base.endsWith('/') ? base : base+'/') || source.toUtf8().size()>2*1024*1024) {clear();return;}
    m_handler->root=base;m_handler->path='/'+QDir(base).relativeFilePath(canonical);m_handler->source=source.toUtf8();m_handler->mermaid=mermaid;m_handler->dark=dark;
    // 每次源文件变动建立新 origin，旧页和其他工作区不能复用资源权限。
    m_handler->host=QUuid::createUuid().toString(QUuid::Id128);
    m_url=QUrl();m_url.setScheme("ex-view");m_url.setHost(m_handler->host);m_url.setPath(m_handler->path);emit changed();
}
void SafeWebProfile::clear() {
    // 换文件立即撤销旧 origin，不能在新页签标题下继续访问旧工作区资源。
    m_handler->root.clear();m_handler->path.clear();m_handler->host.clear();m_handler->source.clear();
    m_url=QUrl("about:blank");emit changed();
}
bool SafeWebProfile::permits(const QUrl &url) const {return url.scheme()=="ex-view" && url.host()==m_handler->host;}
}
