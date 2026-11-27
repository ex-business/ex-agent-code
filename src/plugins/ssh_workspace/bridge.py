# SSH请求只接收JSON；路径、分支和命令不会拼接进SSH启动命令。
import codecs
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import uuid


def atomic(path, value):
    fd, name = tempfile.mkstemp(prefix='.write-', dir=str(path.parent))
    with os.fdopen(fd, 'w') as stream:
        json.dump(value, stream, ensure_ascii=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(name, path)


RUNNER = r'''
import json,os,pathlib,subprocess,sys,tempfile,time
root=pathlib.Path(sys.argv[1])
def save(value):
 fd,name=tempfile.mkstemp(prefix='.state-',dir=str(root))
 with os.fdopen(fd,'w') as f:
  json.dump(value,f);f.flush();os.fsync(f.fileno())
 os.replace(name,root/'state.json')
try:
 job=json.loads((root/'request.json').read_text())
 state={'id':job['id'],'state':1,'pid':os.getpid(),'startedAt':time.time(),'storedBytes':0,'totalBytes':0}
 try:
  env=dict(os.environ,LC_ALL='C');state['processStart']=subprocess.run(['ps','-p',str(os.getpid()),'-o','lstart='],capture_output=True,text=True,env=env,timeout=5).stdout.strip()
 except Exception:pass
 save(state)
 child=subprocess.Popen(['/bin/sh','-lc',job['script']],cwd=job['cwd'],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 with (root/'output.log').open('ab',buffering=0) as log:
  while True:
   data=child.stdout.read1(65536)
   if not data:break
   room=max(0,16*1024*1024-state['storedBytes'])
   log.write(data[:room]);state['storedBytes']+=min(room,len(data));state['totalBytes']+=len(data)
 code=child.wait();state.update(state=2 if code==0 else 3,exitCode=code,finishedAt=time.time())
 save(state)
except Exception as error:
 save({'id':root.name,'state':3,'detail':str(error)[:2048]})
'''


def git(cwd, *args):
    p = subprocess.run(['git', '-C', str(cwd), *args], stdin=subprocess.DEVNULL,
                       capture_output=True, timeout=30)
    if p.returncode:
        raise ValueError(p.stderr.decode('utf-8', 'replace')[:4096])
    return p.stdout.decode('utf-8', 'replace').strip()


def path(value):
    if not isinstance(value, str) or not value.startswith('/') or '\x00' in value or len(value) > 4096:
        raise ValueError('远程目录必须是绝对路径。')
    p = pathlib.Path(value).resolve(strict=True)
    if not p.is_dir():
        raise ValueError('远程目录不存在。')
    return p


def main(p):
    action = p['action']
    if action == 'probe':
        return {'python': sys.version.split()[0], 'git': git(pathlib.Path.home(), '--version'),
                'home': str(pathlib.Path.home()), 'platform': sys.platform}
    root = pathlib.Path(p.get('storage') or '~/.local/share/ex-agent-code/ssh').expanduser()
    if not root.is_absolute() or root.is_symlink():
        raise ValueError('SSH运行存储目录无效。')
    root.mkdir(parents=True, mode=0o700, exist_ok=True)
    st = root.stat()
    if st.st_uid != os.getuid() or st.st_mode & 0o077:
        raise ValueError('SSH运行存储目录必须仅当前用户可访问。')
    if action == 'inspect':
        cwd = path(p['path'])
        return {'path': str(cwd), 'repository': git(cwd, 'rev-parse', '--show-toplevel'),
                'branch': git(cwd, 'branch', '--show-current'), 'head': git(cwd, 'rev-parse', 'HEAD')}
    if action == 'worktree':
        cwd = path(p['path'])
        branch, start = p['branch'], p['start']
        if not isinstance(branch, str) or not branch or branch.startswith('-') or branch.lower().startswith('code') or len(branch) > 200:
            raise ValueError('分支名称无效。')
        git(cwd, 'check-ref-format', '--branch', branch)
        if not isinstance(start, str) or not start or start.startswith('-') or len(start) > 200:
            raise ValueError('起点无效。')
        commit = git(cwd, 'rev-parse', '--verify', '--end-of-options', start + '^{commit}')
        destination = root / 'worktrees' / str(uuid.UUID(p['id']))
        destination.parent.mkdir(mode=0o700, exist_ok=True)
        # 相同请求只核对已创建的目录，不重复运行git或清理用户数据。
        if destination.exists():
            if git(destination, 'branch', '--show-current') != branch:
                raise ValueError('保留的工作树与请求分支不一致。')
        else:
            git(cwd, 'worktree', 'add', '-b', branch, str(destination), commit)
        return {'path': str(destination), 'branch': branch, 'head': git(destination, 'rev-parse', 'HEAD')}
    run_id = str(uuid.UUID(p['id']))
    directory = root / 'runs' / run_id
    if action == 'launch':
        cwd = path(p['path'])
        script = p['script']
        if not isinstance(script, str) or not script.strip() or len(script) > 65536 or '\x00' in script:
            raise ValueError('远程命令无效或超过64K字符。')
        request = {'id': run_id, 'cwd': str(cwd), 'script': script}
        directory.parent.mkdir(mode=0o700, exist_ok=True)
        try:
            directory.mkdir(mode=0o700)
        except FileExistsError:
            if json.loads((directory / 'request.json').read_text()) != request:
                raise ValueError('同一运行ID对应了其他命令。')
            return {'id': run_id, 'state': json.loads((directory / 'state.json').read_text())['state']}
        atomic(directory / 'request.json', request)
        atomic(directory / 'state.json', {'id': run_id, 'state': 0})
        # 独立会话及关闭全部SSH文件描述符，客户端断线不终止远程任务。
        subprocess.Popen([sys.executable, '-c', RUNNER, str(directory)], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         close_fds=True, start_new_session=True)
        return {'id': run_id, 'state': 0}
    if action == 'read':
        state = json.loads((directory / 'state.json').read_text())
        # 断线不代表完成；进程消失或PID已复用时仅标为待核对，不重启旧命令。
        if state['state'] == 1:
            try:
                os.kill(state['pid'], 0)
                if state.get('processStart'):
                    current = subprocess.run(['ps', '-p', str(state['pid']), '-o', 'lstart='],
                                             capture_output=True, text=True, env=dict(os.environ, LC_ALL='C'), timeout=5)
                    if current.stdout.strip() != state['processStart']:
                        state.update(state=4, detail='远程运行进程身份已变化，结果待核对。')
            except (OSError, subprocess.SubprocessError):
                state.update(state=4, detail='远程运行进程不可确认，任务结果待核对。')
        elif state['state'] == 0 and time.time() - (directory / 'state.json').stat().st_mtime > 30:
            state.update(state=4, detail='远程启动记录未完成确认，请核对后再操作。')
        if state['state'] == 4:
            latest = json.loads((directory / 'state.json').read_text())
            if latest['state'] in (2, 3):
                state = latest
        offset = p.get('offset', 0)
        if type(offset) is not int or offset < 0 or offset > 2**53-1:
            raise ValueError('输出偏移无效。')
        log = directory / 'output.log'
        text = ''
        size = log.stat().st_size if log.exists() else 0
        if offset > size:
            raise ValueError('输出偏移超过日志长度，请从0重读。')
        if size:
            with log.open('rb') as stream:
                stream.seek(offset)
                data = stream.read(65536)
            decoder = codecs.getincrementaldecoder('utf-8')('replace')
            text = decoder.decode(data, final=offset + len(data) >= size and state['state'] in (2, 3))
            offset += len(data) - len(decoder.getstate()[0])
        state.update(text=text, offset=offset, size=size)
        return state
    raise ValueError('未知SSH操作。')


try:
    raw = sys.stdin.buffer.read(262145)
    if len(raw) > 262144:
        raise ValueError('SSH请求超过容量。')
    result = main(json.loads(raw))
    print(json.dumps({'ok': True, 'result': result}, ensure_ascii=False))
except Exception as error:
    print(json.dumps({'ok': False, 'error': str(error)[:4096]}, ensure_ascii=False))
