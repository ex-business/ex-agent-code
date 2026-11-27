# encoding: UTF-8
require 'yaml'
require 'fileutils'
require 'open3'
require 'find'

# 只读普通配置文件，禁止配置符号链接或超大配置间接读取仓库外的数据。
def config_text(root, name)
  path = File.join(root, name)
  return '' unless File.exist?(path) || File.symlink?(path)
  raise "配置文件无效：#{name}" unless File.file?(path) && !File.symlink?(path)
  text = File.open(path, 'rb') { |f| f.read(262145) || '' }
  raise "配置文件过大：#{name}" if text.bytesize > 262144
  text.force_encoding('UTF-8')
end

def config(root)
  text = config_text(root, 'orca.yaml')
  return {} if text.strip.empty?
  data = YAML.safe_load(text)
  raise 'orca.yaml 顶层必须是映射' unless data.is_a?(Hash)
  data
end

def relative_path(entry)
  return unless entry.is_a?(String)
  path = entry.strip.tr('\\', '/').sub(%r{\A\./}, '').sub(%r{/\z}, '')
  return if path.empty? || path.start_with?('!', '/') || path.match?(/\A[A-Za-z]:|[\x00-\x1f*?]/)
  return if path.split('/', -1).any? { |s| s.empty? || %w[. .. .git].include?(s.downcase) }
  path
end

def git(*args)
  output, status = Open3.capture2('git', *args)
  raise '读取 Git 工作树信息失败' unless status.success?
  output
end

def prepare_paths(primary, target)
  shares = config(primary).dig('worktree', 'sharedDirectories') || []
  raise 'sharedDirectories 必须是列表' unless shares.is_a?(Array)
  copies = config_text(primary, '.worktreeinclude').lines.map(&:strip).reject { |s| s.start_with?('#') || s.empty? }
  bytes = entries = 0
  [[shares.first(100), true], [copies.first(1000), false]].each do |paths, share|
    paths.map { |s| relative_path(s) }.compact.uniq.each do |rel|
      src = File.join(primary, rel)
      dst = File.join(target, rel)
      next unless File.exist?(src) && File.realpath(src) == src
      next if File.exist?(dst) || File.symlink?(dst)
      next if share && !File.directory?(src)
      # 已链接的父目录归主检出所有，不能经由它写入；也不搬运强制跟踪的文件。
      parent = File.dirname(dst)
      parent = File.dirname(parent) until File.exist?(parent) || File.symlink?(parent)
      next if File.symlink?(parent) || File.realpath(parent) != parent
      ignored = system('git', '-C', primary, 'check-ignore', '-q', '--', rel)
      next if !ignored && $?.exitstatus == 1
      raise "检查忽略规则失败：#{rel}" unless ignored
      next unless git('-C', primary, '--literal-pathspecs', 'ls-files', '--', rel).empty?
      FileUtils.mkdir_p(File.dirname(dst))
      if share
        File.symlink(src, dst)
      else
        Find.find(src) do |p|
          raise "复制目录包含 Git 元数据：#{rel}" if File.basename(p).casecmp?('.git')
          st = File.lstat(p)
          raise "复制路径含链接或特殊文件：#{rel}" unless st.file? || st.directory?
          entries += 1
          bytes += st.size if st.file?
          raise '忽略文件复制超出容量限制' if entries > 50000 || bytes > 2 * 1024**3
        end
        FileUtils.cp_r(src, dst, preserve: true)
      end
    end
  end
end

begin
  source, target, policy = ARGV
  list = git('-C', source, 'worktree', 'list', '--porcelain', '-z')
  primary = File.realpath(list.split("\0", 2).first.sub(/\Aworktree /, ''))
  target = File.realpath(target)
  prepare_paths(primary, target)
  script = config(target).dig('scripts', 'setup') if policy == 'run'
  if script && script != ''
    raise 'scripts.setup 必须为字符串' unless script.is_a?(String)
    env = {'ORCA_ROOT_PATH'=>primary, 'ORCA_WORKTREE_PATH'=>target, 'ORCA_WORKSPACE_NAME'=>File.basename(target)}
    # 日志放在工作树外，避免仓库控制日志目的地。失败保留现场供用户排查。
    log = target + '.setup.log'
    File.open(log, File::WRONLY | File::CREAT | File::EXCL, 0600) do |io|
      ok = system(env, '/bin/bash', '-e', '-c', script, chdir: target, in: File::NULL, out: io, err: io)
      raise "setup 执行失败（退出码 #{$?.exitstatus}），日志：#{log}" unless ok
    end
  end
rescue StandardError => e
  STDERR.puts "工作树准备失败：#{e.message}"
  exit 1
end
