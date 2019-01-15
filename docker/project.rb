# frozen_string_literal: true

class Project
  def initialize(name, dockerfile:, image:, mounts: [], commands: {}, run_as_root: false, build_options: [], options: nil)
    @name = name
    @dockerfile = File.join(File.expand_path(File.dirname(__FILE__)), dockerfile)
    @image = image
    @mounts = mounts
    @commands = commands
    @run_as_root = run_as_root
    @build_options = build_options
    @options = options
  end

  def build(quiet: true)
    raise "cannot build #@name" if ![@dockerfile, @image].all?
    system("docker", "build", *(quiet ? ["-q"] : []), *@build_options, "-f", @dockerfile, "-t", @image, File.dirname(File.dirname(__FILE__)))
  end

  def command(name, popen: false)
    raise "cannot run command for #@name" if !@image
    commands = @commands[name.intern] or raise "command #{name} not found for #@name"
    commands = [commands] if commands[0].is_a?(String)

    if popen
      raise "cannot popen multiple commands" if commands.length > 1
      run("--", *commands[0], popen: true)
    else
      commands.each do |command|
        run("--", *command)
      end
    end
  end

  def run(*cmd, popen: false)
    raise "cannot run #@name" if !@image
    argv = []
    argv.concat(@options) if @options && (cmd.length == 0 || cmd[0] == '--')
    while cmd.any? && cmd[0] != '--'
      argv << cmd.shift
    end
    @mounts.each { |m| argv << "-v" << m }
    argv << @image
    cmd.shift if cmd[0] == '--'
    argv.concat(cmd)

    args = ["docker", "run", *user_args, "--pid=host", "--rm", "--name", docker_container_name, *argv]
    if popen
      IO.popen(args, "r+", err: [:child, :out])
    else
      system(*args)
    end
  end

  def exec(*cmd)
    raise "cannot exec #@name" if !@image
    argv = []
    while cmd.any? && cmd[0] != '--'
      argv << cmd.shift
    end
    argv << docker_container_name
    cmd.shift if cmd[0] == '--'
    argv.concat(cmd)
    system("docker", "exec", *user_args, "-i", *argv)
  end

  def docker_container_name
    "projfs-#@name"
  end

  def user_args
    if @run_as_root
      []
    else
      ["-u", Process.uid.to_s]
    end
  end
end

# vim: set sw=2 et:
