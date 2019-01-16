# frozen_string_literal: true
#
# Linux Projected Filesystem
# Copyright (C) 2019 GitHub, Inc.
#
# See the NOTICE file distributed with this library for additional
# information regarding copyright ownership.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library, in the file COPYING; if not,
# see <http://www.gnu.org/licenses/>.

def report(name, text)
  text.split("\n").each do |line|
    puts "#{name}: #{line}"
  end
end

def wait_for(name, io, msgs, timeout: 1)
  buf = String.new
  start = Time.now

  while !(msg = msgs.find {|m| buf.include?(m)})
    begin
      s = io.read_nonblock(1024)
    rescue IO::WaitReadable
      remaining = timeout - (Time.now - start)
      raise "timed out waiting for #{msg.inspect}" if remaining <= 0
      any = IO.select([io], nil, nil, remaining)
      retry if any
      raise "timed out waiting for one of #{msgs.inspect}"
    rescue EOFError
      raise "timed out waiting for one of #{msgs.inspect} (got EOF)"
    end

    report(name, s)
    buf << s
  end

  msg
end

MSG_PRESS_ENTER = "Press Enter to end"
MSG_CONFLICT = "Conflict. The container name"

def tests(images, force: false)
  integration = images["integrate"].command("mount", popen: true)
  msg = wait_for("integrate", integration, [MSG_PRESS_ENTER, MSG_CONFLICT], timeout: 5)
  if msg == MSG_CONFLICT
    puts "integration container already running"
    system "docker", "ps", "-f", "name=#{images["integrate"].docker_container_name}"
    if !force
      raise "cannot run tests; remove container or do so automatically with --force"
    end

    system "docker", "stop", "-t", "0", images["integrate"].docker_container_name

    integration = images["integrate"].command("mount", popen: true)
    msg = wait_for("integrate", integration, [MSG_PRESS_ENTER, MSG_CONFLICT], timeout: 5)
    if msg == MSG_CONFLICT
      raise "still couldn't start container"
    end
  end

  puts "test: checking that touching a file is recognised"

  id = 16.times.map { ("a".."z").to_a.sample(1) }.join
  images["integrate"].exec("--", "touch", "TestRoot/src/#{id}")
  msg = wait_for("integrate", integration, ["OnNewFileCreated (isDirectory: False): #{id}"])
  images["integrate"].exec("--", "rm", "TestRoot/src/#{id}")
  msg = wait_for("integrate", integration, ["OnPreDelete (isDirectory: False): #{id}"])

  puts "test: finished; stopping mount gracefully"

  integration.write "\n"
  integration.flush
  integration.close_write
  report("integrate", integration.read)
  integration.close

  puts "test: done"
end

# vim: set sw=2 et:
