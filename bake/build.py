# This file must be run with special variables in the global scope.
#   roots: list containing starting source files (ex ['main.cxx'])
#   linker: list of tokens to indicate linker program (ex ['g++'])
#   link_tail: function from set of libraries to list of tokens to
#              go after list of object files being linked
#              but before other libraries to link against
#   compiler: function mapping filenames to list of command line tokens
#             for compiler (ex lambda src: ['g++'])
#   h_lib: function mapping headers to library names (ex "mpi.h" -> "mpich2")
#   lib_path: function mapping library names to -L paths (ex "mpich2" -> "/usr/lib/mpich2")
#   lib_libs: function mapping library names to lists of dependency library naems
#             (ex "hdf5" -> ["z"])

import async
import bake
import gxx_crawl
import os
import re
import subprocess
import sys

returncode = [None]

def main_a():
  def argroot(x):
    def env_eval(a, deft=None):
      s = os.environ.get(a)
      return deft if s is None else eval(s,{},{})
    
    if x in ('roots','linker','link_tail'):
      return globals()[x]
    elif isinstance(x, tuple) and len(x)==2:
      if x[0] == 'env':
        return os.environ.get(x[1])
      elif x[0] in ('compiler','h_lib','lib_flags','lib_libs'):
        return globals()[x[0]](x[1])
      else:
        return None
    else:
      return None
  
  class Config(bake.Config):
    path = ".oven"
    def make_host(me):
      return bake.MemoHost(bake.FileHost)
    def arg_group(me, x):
      return 0
    def input_group(me, x):
      return 0
  
  oven = yield async.Sync(bake.Oven.new_a(Config()))
  
  try:
    exe = yield async.Sync(oven.memo_a(gxx_crawl.crawl_a, argroot))
    returncode[0] = 0
    sys.stdout.write(exe)
  except OSError, e:
    returncode[0] = e.errno
    raise
  except subprocess.CalledProcessError, e:
    returncode[0] = e.returncode
  yield async.Sync(oven.close_a())

try:
  async.run(main_a())
except OSError, e:
  print>>sys.stderr, 'ERROR:', e.strerror, getattr(e,'filename',None) or ''  
except Exception, e:
  if hasattr(e, 'async_traceback'):
    print >> sys.stderr, e.async_traceback
  raise

if returncode[0] != 0:
  print >> sys.stderr, 'HALF BAKED: error occurred.'
exit(returncode[0])
