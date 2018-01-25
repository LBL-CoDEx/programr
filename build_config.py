# The job of this file is to produce the following variables:
#   compiler: string -> [string], maps a source file to the command line tokens that build it
#   linker: [string], tokens used for linking
#   link_tail: [string], tokens to go after object files on link line but before other libraries
#   h_lib: string -> string, maps a header file path to a library name ('mpi.h'->'mpi')
#   lib_flags: string -> [string], maps a library name to its linker flags ('mpi'->['-lmpi'])

# You can expect that these variables are magically in global scope:
#   cxx_flags: list of strings for c++ compiler options (ex. ['-O2','-Wall'])
#   cc_flags: list of strings for c compiler options (ex. ['-O2','-Wall'])
#   ld_flags_0: list of strings for linker options (before list of object files)
#   ld_flags_1: list of strings for linker options (after list of ojbect files, before other libs)

import os
import re
import subprocess as subp
import sys

# read `nm` from the environment or error if not present
def E(nm, deft=None):
  try:
    return os.environ[nm]
  except:
    if deft is None:
      sys.stderr.write('Environment variable ' + nm + ' not set!\n')
      exit(1)
    else:
      return deft

# (string,string -> string) Extracts a variable's value from a makefile.
def M(makefile, var):
  p = subp.Popen(['make','-f','-','gimme'], stdin=subp.PIPE, stdout=subp.PIPE, stderr=subp.PIPE)
  tmp = ('include {0}\n' + 'gimme:\n' + '\t@echo $({1})\n').format(makefile, var)
  val = p.communicate(tmp)[0]
  val = val.strip(' \t\n')
  if p.returncode != 0:
    sys.stderr.write("Makefile " + makefile + " does not exist.\n")
    exit(p.returncode)
  return val

# default cxx and cc compilers
def deft_cxx_cc():
  return 'g++ -std=c++11', 'gcc -std=gnu11'

# determine cc/cxx compilers, first check environment, then deft_cxx_cc
cxx = (E('CXX','') or deft_cxx_cc()[0]).split() # tokenize
cc = (E('CC','') or deft_cxx_cc()[1]).split() # tokenize

# find the knobs
knobs = []
for k,v in os.environ.items():
  m = re.match(r'PROGRAMR_(KNOB_.*)$', k)
  if m is not None:
    f = '-D' + m.group(1) + '=' + v
    knobs.append(f)

cxx_flags += knobs
cc_flags += knobs

# ======================================================================
# Externally used things
# ======================================================================

def compiler(src):
  # list of (regex, lambda) where each regex which matches src will have
  # its lambda applied to the list in turn.
  patterns = [
    (r'.*\.cxx', lambda x: x + cxx + cxx_flags),
    (r'.*\.c', lambda x: x + cc + cc_flags),
  ]
  
  acc = []
  for pat,act in patterns:
    if re.match(pat+'$', src) is not None:
      acc = act(acc)
  return acc

linker = cxx + ld_flags_0

link_tail = ld_flags_1

def h_lib(h):
  # first match wins
  patterns = [
    (r'.*/gasnet.h', 'gasnet'),
    (r'.*/pthread.h', 'pthread'),
    (r'.*/metis.h', 'metis'),
  ]
  
  for pat,lib in patterns:
    if re.match(pat+'$', h) is not None:
      return lib
  return None

def lib_flags(lib):
  if lib == 'gasnet':
    return M(E('GASNET_mak'), 'GASNET_LIBS').split()
  elif lib == 'pthread':
    return ['-pthread']
  else:
    return ['-l' + lib]

def lib_libs(lib):
  deft = [] # default
  return {
  }.get(lib, deft)
