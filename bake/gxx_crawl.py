import bake
async = bake.async
valtool = bake.valtool

import os
import shlex
import sys

def relnormpath(path):
  a = os.path.normpath(os.getcwd()) + os.path.sep
  b = os.path.normpath(path)
  
  if b.startswith(a):
    return b[len(a):]
  else:
    return path

def includes_a(ctx):
  """returns a list of paths which are the files this source file is dependent on (list includes this file)
  args: "path":string, "g++":[string]
  """
  path = ctx['path']
  sys_hs = ctx['system_headers']
  cc = ctx['compiler', path]
  
  yield async.Sync(ctx.infile_a(path))
  
  if cc and os.path.exists(path):
    cmd = bake.Cmd(ctx)
    M = "-M" if sys_hs else "-MM"
    cmd.lit(cc,M,"-MT","x").inf(path)
    yield async.Sync(cmd.exec_a())
    
    rule = cmd.stdout
    rule = rule[rule.index(":")+1:]
    
    deps = shlex.split(rule.replace("\\\n",""))[1:] # first is source file
    deps = map(relnormpath, deps)
    deps = frozenset(deps)
    
    yield async.Sync(ctx.infiles_a(deps))
    
    yield async.Result(deps)
  else:
    yield async.Result(())

@valtool.follow(includes_a)
def compile_a(ctx):
  """returns a path to the compiled object file, or None if source file does not exist
  args: "path":string, "g++":[string]
  """
  path = ctx['path']
  
  yield async.Sync(ctx.infile_a(path))
  
  if os.path.exists(path):
    cc = ctx['compiler', path]
    if cc:
      # execute shell command like: $(g++) $(CXXFLAGS)-c $(path) -o $o
      cmd = bake.Cmd(ctx)
      cmd.lit(cc, "-c").inf(path).lit("-o").outf(path+'.o')
      yield async.Sync(cmd.exec_a())
      
      # register header dependencies
      deps = yield async.Sync(ctx(includes_a, {'path':path, 'system_headers':False}))
      yield async.Sync(ctx.infiles_a(deps))
      
      yield async.Result(cmd.outs[path+'.o'])
    else:
      yield async.Result(path)
  else:
    yield async.Result(None)

@valtool.follow(includes_a)
@valtool.follow(compile_a)
def crawl_a(ctx):
  """given a source paths 'roots' to some source files, will compile those and all other source files found by matching
  included '.hxx' files with corresponding '.cxx' files.  then all the objects get linked and the exe path is returned
  args: "roots":[string]
  """
  linker,roots = ctx.argseq('linker','roots')
  
  more = map(relnormpath, roots)
  objs = {} # maps source file paths to compiled object files, or None if source does not exist
  incs = set() # all headers seen including system headers
  futtag = {}
  while True:
    fresh = [p for p in more if p not in objs]
    ctx.infiles_a(fresh)

    for p in fresh:
      objs[p] = None
      if os.path.exists(p):
        fut = yield async.Begin(ctx(includes_a, {'path':p, 'system_headers':False}))
        futtag[fut] = ('inc',p)
        fut = yield async.Begin(ctx(includes_a, {'path':p, 'system_headers':True}))
        futtag[fut] = ('inc_sys',p)
        fut = yield async.Begin(ctx(compile_a, {'path':p}))
        futtag[fut] = ('obj',p)
    del more[:]
    
    if len(futtag) == 0:
      break
    fut = yield async.WaitAny(futtag)
    tag, p = futtag.pop(fut)
    res = fut.result()
    #sys.stderr.write('got %r %r %r\n' % (tag, p, res))
    
    if tag == 'obj':
      objs[p] = res
    elif tag == 'inc':
      for inc in res:
        base, ext = os.path.splitext(inc)
        if ext in ('.h','.hpp','.hxx'):
          for ext in ('.c','.cpp','.cxx','.C'):
            more.append(base + ext)
    elif tag == 'inc_sys':
      incs.update(res)
    else:
      assert False
  
  def topsort(xs, deps):
    ans = []
    ansset = set()
    def visit(x):
      if x not in ansset:
        ansset.add(x)
        ans.append(x)
        for y in deps(x):
          visit(y)
    for x in xs:
      visit(x)
    return ans
  
  libs = set(ctx.args(('h_lib',inc) for inc in incs).itervalues()) - set([None])
  lib_libs = ctx.argdict((lib,('lib_libs',lib)) for lib in libs)
  libs = topsort(libs, lambda x: lib_libs[x])
  lib_flags = ctx.argdict((lib,('lib_flags',lib)) for lib in libs)
  
  # link it
  cmd = bake.Cmd(ctx)
  cmd.\
    lit(linker).\
    lit("-o").outf("a.out").\
    infs(o for o in objs.values() if o).\
    lit(ctx['link_tail']).\
    lit(lib_flags[lib] for lib in libs)
  yield async.Sync(cmd.exec_a())
  yield async.Result(cmd.outs["a.out"])
