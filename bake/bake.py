import array
import async
import binascii
import collections
import hashlib
import os
import random
import shutil
import sqlite3
import struct
import subprocess
import sys
import types
import valtool

deque = collections.deque
_bin2hex = binascii.hexlify
_hex2bin = binascii.unhexlify

def _reiterable(xs):
  if type(xs) in (tuple, list, set, frozenset, dict):
    return xs
  else:
    return tuple(xs)

def _valhash(x):
  return valtool.Hasher().eat(x).digest()

def _seqhash(xs):
  return valtool.Hasher().eatseq(xs).digest()

def _ensure_dirs(path):
  d = os.path.split(path)[0]
  if not os.path.exists(d):
    os.makedirs(d)

def _remove_clean(keep, rest):
  """Remove file or tree at 'os.path.join(keep,rest)'.  Then remove all empty dirs
  up to but not including 'keep'.  Does not fail when things don't exist."""
  assert not os.path.isabs(rest)
  rest = rest.rstrip(os.path.sep)
  
  if rest != '':
    p = os.path.join(keep, rest)
    if os.path.isdir(p):
      shutil.rmtree(p)
    elif os.path.isfile(p):
      os.remove(p)
    
    while True:
      rest = os.path.dirname(rest)
      if rest == '': break
      p = os.path.join(keep, rest)
      try:
        os.rmdir(p)
      except:
        if os.path.isdir(p):
          break

def _sql_ensure_table(cxn, name, cols, ixs=()):
  cur = cxn.cursor()
  cur.execute("select name from sqlite_master where type='table' and name=?", (name,))
  if cur.fetchone() is None:
    cur.execute("create table " + name + "(" + ",".join(cols) + ")")
    i = 0
    for ix in ixs:
      cur.execute("create index " + ("%s__index_%d"%(name,i)) + " on " + name + "(" + ",".join(ix) + ")")
      i += 1

def _flatten(x):
  if getattr(x, "__iter__", False):
    for it in x:
      for y in _flatten(it):
        yield y
  else:
    yield x

class Cmd(object):
  def __init__(me, ctx, cwd=None, env=None, executable=None, tag=None,
               pool=None, showout=False, showerr=True):
    me._ctx = ctx
    me._infs = set()
    me._toks = []
    me._oxs = {}
    me.cwd = cwd
    me.env = env or dict(os.environ)
    me.executable = executable # if os.name == 'nt' else None
    me.pool = pool
    me.tag = tag
    me.showout = showout
    me.showerr = showerr
  
  def lit(me, *toks):
    me._toks += _flatten(toks)
    return me
  
  def inf(me, path, fmt="%s"):
    path = os.path.normpath(path)
    me._infs.add(path)
    me._toks.append(fmt % path)
    return me
  
  def infs(me, paths, fmt="%s"):
    for p in paths:
      path = os.path.normpath(p)
      me._infs.add(path)
      me._toks.append(fmt % path)
    return me
  
  def outf(me, path, fmt="%s"):
    me._oxs[path] = (len(me._toks), fmt)
    me._toks.append(None)
    return me
  
  def prepare_a(me):
    yield async.Sync(me._ctx.infiles_a(me._infs))
    
    for o in me._oxs:
      ix, fmt = me._oxs[o]
      me._oxs[o] = fmt % (yield async.Sync(me._ctx.outfile_a(o)))
      me._toks[ix] = me._oxs[o]
    
    me.shline = subprocess.list2cmdline(me._toks)
    me.outs = me._oxs
  
  def exec_a(me):
    if not hasattr(me, 'shline'):
      yield async.Sync(me.prepare_a())
    
    @async.assign_pool(me.pool)
    def go():
      pipe = subprocess.PIPE
      try:
        p = subprocess.Popen(me._toks, cwd=me.cwd, env=me.env, stdin=pipe, stdout=pipe, stderr=pipe)
      except OSError, e:
        e.filename = getattr(e,'filename',None) or me._toks[0]
        raise e
      me.stdout, me.stderr = p.communicate()
      me.returncode = p.returncode
    
    if me.tag is not None:
      tag = me.tag + ': '
    else:
      tag = ''
    
    if me.showout or me.showerr:
      #print>>sys.stderr, '[RUN] ' + tag + me.shline
      print>>sys.stderr, tag + me.shline
      
    yield async.Sync(go)
    
    if me.showerr and me.stderr != '':
      print>>sys.stderr, '-'*72 + '\n[MSG] ' + tag + me.shline + '\n\n' + \
        me.stderr + ('' if me.stderr[-1] == '\n' else '\n') + '-'*72
    
    if me.showout and me.stdout != '':
      print>>sys.stderr, '-'*72 + '\n[OUT] ' + tag + me.shline + '\n\n' + \
        me.stdout + ('' if me.stdout[-1] == '\n' else '\n') + '-'*72
    
    if me.returncode != 0:
      raise subprocess.CalledProcessError(me.returncode, me.shline)

class Host(object):
  def canonify(me, x):
    return x
  def lift_file(me, path):
    assert False
  def unlift_file(me, x):
    assert False
  def query_a(me, keys, stash):
    assert False

class MemoHost(Host):
  """Given a host, memoize it so that redundant key lookups are cached.  This
  makes sense when we expect the state of the world to remain frozen for the
  lifetime of this host object.
  """
  def __init__(me, host):
    me.host = host
    me.cache = {}
  
  def canonify(me, x):
    return me.host.canonify(x)
  
  def lift_file(me, path):
    return me.host.lift_file(path)
  
  def unlift_file(me, x):
    return me.host.unlift_file(x)
  
  def query_a(me, keys, stash):
    host = me.host
    cache = me.cache
    keys = keys if type(keys) is set else set(keys)
    vals = {}
    for k in keys:
      if k in cache:
        vals[k] = cache[k]
    if len(vals) != len(keys):
      keys1 = tuple(k for k in keys if k not in vals)
      vals1 = yield async.Sync(host.query_a(keys1, stash))
      vals.update(vals1)
      cache.update(vals1)
      assert len(vals) == len(keys)
    yield async.Result(vals)

class _FileHost(Host):
  """A host whose keys are interpreted as filesystem paths, the returned hashes values are content hashes.
  """
  def __call__(*a,**kw):
    raise Exception("FileHost is an instance, not a constructor!")
  
  def canonify(me, x):
    return os.path.abspath(x)
  
  def lift_file(me, path):
    return path
  
  def unlift_file(me, x):
    return x
  
  def query_a(me, paths, stash):
    def action(path, old):
      t0, h0 = old if old is not None else (0, '')
      if os.path.exists(path):
        t1 = int(os.path.getmtime(path)*10000)
        if t0 != t1:
          md5 = hashlib.md5()
          with open(path, 'rb') as f:
            for b in iter(lambda: f.read(8192), ''):
              md5.update(b)
          h1 = md5.digest()
        else:
          h1 = h0
      else:
        t1, h1 = 0, ''
      return (t1, h1) if (t1, h1) != (t0, h0) else old
    reals = dict((p, os.path.realpath(p)) for p in paths)
    ans = yield async.Sync(stash.updates_a(reals.keys(), action))
    ans = dict((k,'' if th is None else th[1]) for k,th in ans.iteritems())
    yield async.Result(ans)

FileHost = _FileHost()

def TestNo(y):
  return MatchNone

class Match(object):
  def inputs_a(me, xs, query_a):
    """given tuple of input names 'xs', returns test over tuples of hashes"""
    assert False
  def args(me, xs):
    """given tuple of arg names 'xs', returns test over tuple of values"""
    assert False
  def result(me, y):
    """reached function return of value y"""
    assert False

class _MatchNone(Match):
  def inputs_a(me, xs, query_a):
    yield async.Result(TestNo)
  def args(me, xs):
    return TestNo
  def result(me, y):
    pass
  def __call__(*a,**b):
    assert False # MatchNone is not a constructor!
MatchNone = _MatchNone()

class TestEqualAny(object):
  def __init__(me, values, next_match):
    """vals: list of values to test equality, next_match: val->Match"""
    me.values = values if isinstance(values, tuple) else tuple(values)
    me.next_match = next_match
  def __call__(me, y):
    return me.next_match(y) if y in me.values else MatchNone

class TestNotEqualAll(object):
  def __init__(me, values, next_match):
    """vals: list of values to test equality, next_match: val->Match"""
    me.values = values if isinstance(values, tuple) else tuple(values)
    me.next_match = next_match
  def __call__(me, y):
    return me.next_match(y) if y not in me.values else MatchNone

class MatchArgs(Match):
  Accept = object()
  
  def __init__(me, argstest, collector,
    seed={}, merge=lambda old,xys:(lambda d:(d.update(xys),d)[1])(dict(old))):
    """accepts only inputs that match current host hash value, defers to
    argstest to generate test lambda for args.
    argstest: takes (acc, xs, next_match), returns tester
    collector: takes ({x:y}, result) for argument name and values x,y
    """
    me._argstest = argstest
    me._collector = collector
    me._acc = seed
    me._merge = merge
  
  def inputs_a(me, xs, query_a):
    hs = yield async.Sync(query_a(xs))
    hs = tuple(hs[x] for x in xs)
    yield async.Result(TestEqualAny((hs,), lambda _: me))
  
  def args(me, xs):
    def next_match(ys):
      xys = dict((xs[i],ys[i]) for i in xrange(len(xs)))
      return MatchArgs(me._argstest, me._collector, me._merge(me._acc, xys), me._merge)
    return me._argstest(me._acc, xs, next_match)
  
  def result(me, ans):
    return me._collector(me._acc, ans)

class Config(object):
  path = '.'
  
  def make_host(me):
    return MemoHost(FileHost)
  
  def arg_group(me, x):
    return None
  def input_group(me, x):
    return None
  def group_comparer(me):
    return cmp
  def group_legible(me, grp):
    return False
  
  def _key_group(me, key):
    return (me.arg_group if key[0]==_keykind_arg else me.input_group)(key[1])
  def _keys_group(me, keys):
    for key in keys:
      return me._key_group(key)
  def _keys_groups(me, keys):
    return frozenset(me._key_group(key) for key in keys)

class Oven(object):
  @classmethod
  def new_a(cls, config):
    assert isinstance(config, Config)
    me = cls()
    me._config = config
    me._host = config.make_host()
    me._path = os.path.abspath(config.path)
    me._dbcxn = None
    me._dbpath = os.path.join(me._path, "db")
    me._dbpool = async.Pool(size=1)
    
    def schema(cxn):
      _sql_ensure_table(cxn, 'outdirs', ('path','bump'), [['path']])
    yield async.Sync(me._dbjob(schema))
    
    me._stash = yield async.Sync(_Stash.new_a(me))
    me._logdb = yield async.Sync(_LogDb.new_a(me))
    yield async.Result(me)
    
  def _dbjob(me, job):
    @async.assign_pool(me._dbpool)
    def wrap():
      if me._dbcxn is None:
        _ensure_dirs(me._dbpath)
        me._dbcxn = sqlite3.connect(me._dbpath, timeout=10*60)
        me._dbcxn.execute('pragma synchronous=off')
      return job(me._dbcxn)
    return wrap
  
  def close_a(me):
    @async.assign_pool(me._dbpool)
    def close_it():
      if me._dbcxn is not None:
        me._dbcxn.commit()
        me._dbcxn.close()
        me._dbcxn = None
    yield async.Sync(close_it)
  
  def config(me):
    return me._config
  
  def host(me):
    return me._host
  
  def query_a(me, keys):
    return me._host.query_a(keys, me._stash)
  
  def _outfile_a(me, path):
    """ returns a tuple (abs-path,stuff), stuff is only used to delete the file later.
    """
    def bump(cxn):
      cur = cxn.cursor()
      cur.execute('select bump from outdirs where path=?', (path,))
      got = cur.fetchone()
      if got is None:
        got = 0
        cur.execute('insert into outdirs(path,bump) values(?,?)', (path,got+1))
      else:
        got = got[0]
        cur.execute('update outdirs set bump=? where path=?', (got+1,path))
      cxn.commit()
      return got
    
    n = yield async.Sync(me._dbjob(bump))
    d,dp = os.path.splitdrive(path)
    if os.path.isabs(path):
      ovpath = os.path.join('o-abs'+('' if d=='' else '.'+d), str(n), dp[1:])
    else:
      ovpath = os.path.join('o-rel'+('' if d=='' else '.'+d), str(n), dp)
    opath = os.path.join(me._path, ovpath)
    assert not os.path.exists(opath)
    _ensure_dirs(opath)
    yield async.Result((opath,ovpath))
  
  def _is_outfile(me, path):
    o = os.path.join(me._path, 'o-')
    p = os.path.abspath(path)
    return p.startswith(o) # ugly, should use os.path.samefile
  
  def _memo_a(me, fun_a, view):
    def calc_a(view1, log):
      ctx = _Context(view1, log)
      try:
        result = yield async.Sync(fun_a(ctx))
      except:
        e = sys.exc_info()
        for ovpath in ctx._outfs:
          _remove_clean(me._path, ovpath)
        raise e[0], e[1], e[2]
      finally:
        for f in ctx._subfuts:
          yield async.Wait(f)
        ctx._bagtip.terminate()
        bags = ctx._bagdag.flattened()
        log.add_bags(bags)
      yield async.Result(result)
    
    funh = _valhash(fun_a)
    #sys.stderr.write('fun %r = %r\n' % (fun_a.func_code.co_name, _bin2hex(funh)))
    log = yield async.Sync(me._logdb.memo_a(funh, view, calc_a))
    yield async.Result(log)
  
  def memo_a(me, fun_a, argroot=None):
    argroot = _UserArgView.easy_root(argroot)
    view = _View.root(me, argroot)
    log = yield async.Sync(me._memo_a(fun_a, view))
    yield async.Result(log.result()) # will throw if fun_a did, but thats ok
  
  def search_a(me, funtest):
    return me._logdb.search_a(funtest)


def argmap_single(f):
  def g(xs, uav):
    ys = {}
    for x in xs:
      uav.scope_push()
      ys[x] = f(x, uav)
      uav.scope_pop()
    return ys
  g._bake_argmap = True
  return g

def argmap_many(f):
  f._bake_argmap = True
  return f


class _Stash(object):
  @classmethod
  def new_a(cls, oven):
    me = cls()
    me._oven = oven
    def schema(cxn):
      _sql_ensure_table(cxn, 'stash', ('hk0','hk1','val'), [['hk0']])
    yield async.Sync(oven._dbjob(schema))
    yield async.Result(me)
  
  def updates_a(me, keys, action):
    size_i = struct.calcsize('<i')
    def go(cxn):
      try:
        cur = cxn.cursor()
        ans = {}
        changed = False
        for k in keys:
          hk = _valhash(k)
          hk0 = struct.unpack_from('<i', hk)[0]
          hk1 = buffer(hk, size_i)
          row = cur.execute("select val from stash where hk0=? and hk1=?", (hk0,hk1)).fetchone()
          old = None if row is None else valtool.unpack(row[0])
          new = action(k, old)
          ans[k] = new
          if row is not None and new is None:
            cur.execute('delete from stash where hk0=? and hk1=?', (hk0,hk1))
            changed = True
          elif row is None and new is not None:
            val = valtool.pack(new)
            cur.execute('insert into stash(hk0,hk1,val) values(?,?,?)', (hk0,hk1,buffer(val)))
            changed = True
          elif old is not new:
            val = valtool.pack(new)
            cur.execute('update stash set val=? where hk0=? and hk1=?', (buffer(val),hk0,hk1))
            changed = True
        if changed:
          cxn.commit()
        return ans
      except:
        cxn.rollback()
        raise
    ans = yield async.Sync(me._oven._dbjob(go))
    yield async.Result(ans)
  
  def gets_a(me, keys):
    return me.updates_a(keys, lambda key,old: old)
  
  def puts_a(me, keyvals):
    return me.updates_a(keyvals.keys(), lambda key,old: keyvals[key])


class _Context(object):
  def __init__(me, view, log):
    assert isinstance(view, _View)
    assert isinstance(log, _WipLog)
    me._view = view
    view.spynote = me
    me._oven = view._oven
    me._config = view._oven._config
    me._bagdag = _BagDag(me._config)
    me._bagtip = me._bagdag.branch()
    me._bagscope = None
    me._log = log
    me._outfs = []
    me._subfuts = []
  
  @staticmethod
  def _spy(note, keyvals):
    if note is not None:
      ctx = note
      assert isinstance(ctx, _Context)
      ctx._log.add_keys(keyvals)
  
  def _add_bag(me, keys):
    if me._bagscope is not None:
      me._bagscope.add_bag(keys)
    else:
      me._bagtip.add_bag(keys)
  
  def arg(me, x):
    me._add_bag(((_keykind_arg,x),))
    return me._view.args((x,), me._spy)[x]
  
  def __getitem__(me, x):
    me._add_bag(((_keykind_arg,x),))
    return me._view.args((x,), me._spy)[x]
  
  def args(me, xs):
    xs = _reiterable(xs)
    me._add_bag((_keykind_arg,x) for x in xs)
    return me._view.args(xs, me._spy)
  
  def argseq(me, *xs):
    me._add_bag((_keykind_arg,x) for x in xs)
    ys = me._view.args(xs, me._spy)
    return (ys[x] for x in xs)
  
  def argdict(me, m):
    m = m if type(m) is dict else dict(m)
    xs = m.values()
    me._add_bag((_keykind_arg,x) for x in xs)
    ys = me._view.args(xs, me._spy)
    return dict((a,ys[m[a]]) for a in m)
  
  def input_a(me, x):
    key = (_keykind_input,x)
    me._add_bag((key,))
    ys = yield async.Sync(me._view.keys_a((key,), me._spy))
    yield async.Result(ys[key])
  
  def inputs_a(me, xs):
    keys = tuple((_keykind_input,x) for x in xs)
    me._add_bag(keys)
    ys = yield async.Sync(me._view.keys_a(keys, me._spy))
    yield async.Result(dict((x,ys[k,x]) for k,x in ys))
  
  def infile_a(me, path):
    host = me._view._oven.host()
    return me.input_a(host.lift_file(path))
  
  def infiles_a(me, paths):
    host = me._view._oven.host()
    return me.inputs_a((host.lift_file(p) for p in paths))
  
  def outfile_a(me, path):
    opath, stuff = yield async.Sync(me._view._oven._outfile_a(path))
    me._outfs.append(stuff)
    yield async.Result(opath)
  
  def __call__(me, *a, **kw):
    return me.memo_a(*a, **kw)
  
  def memo_a(me, fun_a, argmap=None):
    argmap = _UserArgView.easy_map(argmap)
    subbags = me._bagtip.branch()
    fut = yield async.Begin(
      me._oven._memo_a(fun_a, _View.child(me._view, argmap)),
      future_receiver=me._subfuts.append
    )
    sublog = yield async.Wait(fut)
    bags = sublog.bags()
    bags = _lift_bags(me._config, me._view, argmap, bags)
    for bag in bags:
      subbags.add_bag(bag)
    subbags.terminate()
    yield async.Result(sublog.result()) # ok if raises
  
  def scope_push(me):
    if me._bagscope is None:
      me._bagscope = _BagScope()
    else:
      me._bagscope.push()
  
  def scope_pop(me):
    if 0 == me._bagscope.pop():
      me._bagscope.submit(me._bagtip)
      me._bagscope = None


_keykind_arg = 0
_keykind_input = 1


class _UserArgView(object):
  def __init__(me, view, spy):
    assert view is None or isinstance(view, _View)
    me._view = view
    me._spy = spy
  
  def args(me, xs):
    return me._view.args(xs, me._spy)
  
  def arg(me, x):
    return me.args((x,))[x]
  
  def __getitem__(me, x):
    return me.args((x,))[x]
  
  def argseq(me, *xs):
    ys = me.args(xs)
    return (ys[x] for x in xs)
  
  def argdict(me, m):
    m = m if type(m) is dict else dict(m)
    ys = me.args(m.values())
    return dict((a,ys[m[a]]) for a in m)
  
  def scope_push(me):
    pass
  def scope_pop(me):
    pass
  
  @staticmethod
  def easy_root(f):
    if f is None:
      return lambda x: None
    elif type(f) is dict:
      return f.get
    else:
      return f
  
  @staticmethod
  def easy_map(f):
    if f is None:
      @argmap_many
      def g(xs, uav):
        return uav.args(xs)
      return g
    elif type(f) is dict:
      @argmap_many
      def f1(xs, uav):
        up = uav.args(set(xs) - set(f))
        return dict((x,f[x] if x in f else up[x]) for x in xs)
      return f1
    else:
      return f

class _View(object):
  @classmethod
  def clone(cls, that):
    assert isinstance(that, _View)
    me = cls()
    me._oven = that._oven
    me._parent = that._parent
    me._argmap = that._argmap
    me._memo = dict(that._memo)
    me.spynote = that.spynote
    return me
  
  @classmethod
  def fresh(cls, that):
    assert isinstance(that, _View)
    me = cls()
    me._oven = that._oven
    me._parent = that._parent
    me._argmap = that._argmap
    me._memo = {}
    me.spynote = that.spynote
    return me
  
  @classmethod
  def root(cls, oven, argroot):
    assert isinstance(oven, Oven)
    me = cls()
    me._oven = oven
    me._parent = None
    me._argmap = lambda xs,uav: dict((x,argroot(x)) for x in xs)
    me._memo = {}
    me.spynote = None
    return me
  
  @classmethod
  def child(cls, parent, argmap):
    assert isinstance(parent, _View)
    assert hasattr(argmap, '_bake_argmap')
    me = cls()
    me._oven = parent._oven
    me._parent = parent
    me._argmap = argmap
    me._memo = {}
    me.spynote = None
    return me
  
  def args(me, xs, spy=None):
    xs = _reiterable(xs)
    xs1 = set(x for x in xs if (_keykind_arg,x) not in me._memo)
    ys = me._argmap(xs1, _UserArgView(me._parent, spy))
    
    if spy is not None:
      spy(me.spynote, dict(((_keykind_arg,x),ys[x]) for x in ys))
    
    for x in xs:
      if x not in xs1:
        ys[x] = me._memo[_keykind_arg,x]
      else:
        me._memo[_keykind_arg,x] = ys[x]
    
    return ys
  
  def keys_a(me, keys, spy=None):
    keys = _reiterable(keys)
    xs = {}
    for k,x in keys:
      if k not in me._memo:
        if k not in xs: xs[k] = []
        xs[k].append(x)
    
    ys = {}

    if _keykind_arg in xs:
      ys1 = me._argmap(xs[_keykind_arg], _UserArgView(me._parent, spy))
      ys.update(((_keykind_arg,x),y) for x,y in ys1.items())

    if _keykind_input in xs:
      ys1 = yield async.Sync(me._oven.query_a(xs[_keykind_input]))
      ys.update(((_keykind_input,x),y) for x,y in ys1.items())

      inps = set((_keykind_input,x) for x in xs[_keykind_input])
      p = me._parent
      while p is not None:
        inps.difference_update(p._memo)
        if len(inps) == 0: break
        ys1 = dict((key,ys[key]) for key in inps)
        p._memo.update(ys1)
        if spy is not None:
          spy(p.spynote, ys1)
        p = p._parent
    
    if spy is not None:
      spy(me.spynote, ys)

    for key in keys:
      if key in me._memo:
        ys[key] = me._memo[key]
      else:
        me._memo[key] = ys[key]
    
    yield async.Result(ys)
  
  def keys_memoed(me, keys):
    return dict((key,me._memo[key]) for key in keys)
  

class _BagScope(object):
  def __init__(me):
    me._root = []
    me._stk = [me._root]

  def push(me):
    me._stk.append([])

  def pop(me):
    s = me._stk.pop()
    if len(me._stk) == 0:
      me._stk = None
      return 0
    if len(s) > 0:
      if len(me._stk[-1]) == 0 or me._stk[-1][-1][0] != 'kids':
        me._stk[-1].append(('kids',[]))
      me._stk[-1][-1][1].append(s)
    return len(me._stk)
  
  def add_bag(me, keys):
    me._stk[-1].append(('bag',_reiterable(keys)))

  def submit(me, tip):
    assert isinstance(tip, _BagDag._Tip)
    def f(p, tip):
      for t,x in p:
        if t == 'bag':
          tip.add_bag(x)
        elif t == 'kids':
          ktips = [tip.branch() for k in x]
          for i in xrange(len(x)):
            f(x[i], ktips[i])
            ktips[i].terminate()
    f(me._root, tip)


class _BagDag(object):
  class _Tip(object):
    def __init__(me, dag, parent):
      me._dag = dag
      dag._open_n += 1
      me._parent = parent
      me._kids = []
      if parent is None or parent._prevs is not None:
        me._prevs = set(() if parent is None else parent._prevs)
        me._keyset = set(() if parent is None else parent._keyset)
      else:
        me._prevs = None
        me._keyset = None
    
    def branch(me):
      kid = _BagDag._Tip(me._dag, me)
      me._kids.append(kid)
      return kid
    
    def add_bag(me, keys):
      if me._prevs is None:
        return # adding to a terminated tip does nothing
      
      keys = tuple(keys)
      config = me._dag._config
      gkeys = {}
      
      for key in keys:
        if key not in me._keyset:
          me._keyset.add(key)
          g = config._key_group(key)
          if g not in gkeys: gkeys[g] = set()
          gkeys[g].add(key)
      
      if len(gkeys) > 0:
        nd = _BagDag._Node(len(me._prevs), gkeys)
        if len(me._prevs) == 0:
          me._dag._roots.append(nd)
        for prev in me._prevs:
          prev.nexts.append(nd)
        me._prevs.clear()
        me._prevs.add(nd)
    
    def terminate(me):
      for k in tuple(me._kids):
        k.terminate()
      if me._parent is not None:
        me._parent._prevs |= me._prevs
        me._parent._keyset.update(me._keyset)
        me._parent._kids.remove(me)
      else:
        me._dag._keyset.update(me._keyset)
      me._dag._open_n -= 1
      me._kids = None
      me._prevs = None
      me._keyset = None
  
  class _Node(object):
    __slots__ = ('prev_n','gkeys','nexts')
    def __init__(me, prev_n, gkeys):
      me.prev_n = prev_n
      me.gkeys = gkeys
      me.nexts = []
  
  def __init__(me, config):
    assert isinstance(config, Config)
    me._open_n = 0
    me._config = config
    me._roots = []
    me._keyset = set()
    me._kids = []
  
  def branch(me):
    return me._Tip(me, None)
  
  def flattened(me):
    assert me._open_n == 0
    config = me._config
    
    card = {}
    for key in me._keyset:
      g = config._key_group(key)
      card[g] = 1 + card.get(g, 0)
    
    def runner():
      nd_gs = dict((nd,set(nd.gkeys)) for nd in me._roots)
      nd_n = dict((nd,nd.prev_n) for nd in me._roots)
      def advance(g):
        for nd in tuple(nd_gs):
          nd_gs[nd].discard(g)
          if len(nd_gs[nd]) == 0:
            del nd_gs[nd]
            for nd1 in nd.nexts:
              if nd1 not in nd_n:
                nd_n[nd1] = nd1.prev_n
                if nd_n[nd1] > 100:
                  sys.stderr.write('prev_n {} {}\n'.format(nd_n[nd1], nd1))
              nd_n[nd1] -= 1
              if nd_n[nd1] == 0:
                nd_gs[nd1] = set(nd1.gkeys)
        return nd_gs
      return nd_gs, advance
    
    best_path = None
    gcmp = config.group_comparer()
    nil = object()
    for rand_seed in xrange(10):
      rand = random.Random(rand_seed)
      path = deque()
      nd_gs, advance = runner()
      while len(nd_gs) > 0:
        gn = []
        g1 = nil
        for nd in nd_gs:
          for g in nd_gs[nd]:
            if card[g] == 1:
              g1 = g if g1 is nil or gcmp(g, g1) < 0 else g1
            else:
              gn.append(g)
        if g1 is not nil:
          g = g1
        else:
          gn.sort(cmp=gcmp)
          g = gn[0 if rand_seed==0 else rand.randint(0,len(gn)-1)]
        path.append(g)
        nd_gs = advance(g)
      if best_path is None or len(path) < len(best_path):
        best_path = path
    
    path = best_path
    flat = []
    flatset = set()
    nd_gs, advance = runner()
    while len(nd_gs) > 0:
      g = path.popleft()
      keys = set()
      for nd in nd_gs:
        keys |= nd.gkeys.get(g, frozenset())
      keys -= flatset
      flatset |= keys
      if len(keys) > 0:
        flat.append(keys)
      nd_gs = advance(g)
    
    return flat

def _lift_bags(config, view, kid_argmap, keyss):
  view = _View.fresh(view) # no memo
  view.spynote = view
  bags = []
  for keys in keyss:
    xs = {}
    for k,x in keys:
      if k not in xs: xs[k] = []
      xs[k].append(x)
    
    dag = _BagDag(config)
    scope = _BagScope()
    
    if _keykind_arg in xs:
      scope.push()
      def spy(tag, ys):
        if tag is view:
          scope.add_bag(ys)
      
      class UAV(_UserArgView):
        def scope_push(me):
          scope.push()
        def scope_pop(me):
          scope.pop()
      
      kid_argmap(xs[_keykind_arg], UAV(view, spy))
      scope.pop()
    
    if _keykind_input in xs:
      scope.push()
      bag = ((_keykind_input,x) for x in xs[_keykind_input])
      scope.add_bag(bag)
      scope.pop()
    
    tip = dag.branch()
    scope.submit(tip)
    tip.terminate()
    bags.extend(dag.flattened())
  
  return bags

class _Log(object):
  def __init__(me, funh):
    me._funh = funh
    me._bags = []
    me._done = False
    me._result = None
    me._err = None
  
  def funh(me):
    return me._funh
  
  def add_bags(me, keyss):
    assert not me._done
    for keys in keyss:
      me._bags.append(set(keys))
  
  def finish(me, result):
    assert not me._done
    me._done = True
    me._result = result
    me._err = None
    me._upon_done()
  
  def explode(me, ex, tb):
    assert not me._done
    me._done = True
    me._err = (ex, tb)
    me._upon_done()
  
  def _upon_done(me):
    pass
  
  def bags(me):
    assert me._done
    return me._bags
  
  def result(me):
    assert me._done
    if me._err is not None:
      raise type(me._err[0]), me._err[0], me._err[1]
    else:
      return me._result
  
  def error(me):
    assert me._done
    return me._err
  
  def shift_bag(me, ix, keys):
    assert me._done
    keys = set(keys)
    missing = set(keys)
    bags = me._bags
    n = len(keys)
    i = ix
    while True:
      if n == 0 or i == len(bags):
        bags.insert(ix, keys)
        return missing
      n0 = len(bags[i])
      missing -= bags[i]
      bags[i] -= keys
      n1 = len(bags[i])
      n -= n0 - n1
      if n1 == 0:
        del bags[i]
      else:
        i += 1

class _WipLog(_Log):
  def __init__(me, funh):
    super(_WipLog, me).__init__(funh)
    me._seen = [] # [([(kind,x)],hash)]
    me._bar = async.Barrier()
    
  def add_keys(me, keyvals):
    ks = {}
    for key in keyvals:
      k,x = key
      if k not in ks: ks[k] = []
      ks[k].append(key)
    for keys in ks.values():
      me._seen.append((keys, _seqhash((key,keyvals[key]) for key in keys)))
    me._bar.fireall()
  
  def _upon_done(me):
    me._bar.fireall()


class _LogDb(object):
  @classmethod
  def new_a(cls, oven):
    assert isinstance(oven, Oven)
    me = cls()
    me._oven = oven
    me._lock = async.Lock()
    me._signew = async.Signal(False, lambda a,b: a or b)
    me._wips = {} # {funh: set(_WipLog)} -- work-in-progress
    me._valenc = {}
    me._valdec = {}
  
    def schema(cxn):
      _sql_ensure_table(cxn, 'logtrie', ('parent','ans_a','ans_b','query'), [['parent','ans_a']])
      _sql_ensure_table(cxn, 'valbag', ('val','hash'), [['hash']])
    yield async.Sync(oven._dbjob(schema))
    
    yield async.Result(me)
  
  _wraptag_ptr = 0
  _chunk_tys = (tuple,list,set,frozenset,dict)
  _wraptag_chunk_tag = dict(zip(_chunk_tys, range(1,1+len(_chunk_tys))))
  _wraptag_chunk_ty = dict(zip(range(1,1+len(_chunk_tys)), _chunk_tys))
  
  _items = {
    tuple: lambda x:x,
    list: lambda x:x,
    set: sorted,
    frozenset: sorted,
    dict: lambda x: sorted(x.iteritems())
  }
  
  if False: # with chunking
    def _pack_control(me, cxn):
      def control(x, cata, wrap):
        if type(x) in (tuple,list,set,frozenset,dict):
          chks = me._chunkify(me._items[type(x)](x))
          if len(chks) > 1:
            wrap(control_chunk, me._wraptag_chunk_tag[type(x)], chks)
          else:
            cata(x, control)
        else:
          cata(x, control)
      def control_chunk(x, cata, wrap):
        n = cata(x, control)
        if n >= 64:
          wrap(control, me._wraptag_ptr, me._encode_val(cxn, x))
      return control
  else: # without chunking
    def _pack_control(me, cxn):
      def control(x, cata, wrap):
        cata(x, control)
      return control
  
  def _unpack_unwrap(me, cxn):
    def unwrap(tag, getcomps):
      if tag == me._wraptag_ptr:
        (row,) = getcomps(1)
        return me._decode_val(cxn, row)
      else:
        (chks,) = getcomps(1)
        items = []
        for chk in chks:
          items.extend(chk)
        return me._wraptag_chunk_ty[tag](items)
    return unwrap
  
  def _encode_val(me, cxn, val):
    v = buffer(valtool.pack(val, me._pack_control(cxn)))
    h = valtool.Hasher().raw(v).digest()
    if h not in me._valenc:
      h1 = struct.unpack_from("<i", h)[0]
      cur = cxn.cursor()
      cur.execute('select rowid,val from valbag where hash=?', (h1,))
      row = None
      for got in cur.fetchall():
        if got[1] == v:
          row = got[0]
          break
      if row is None:
        cur.execute('insert into valbag(val,hash) values(?,?)', (v,h1))
        row = cur.lastrowid
      me._valenc[h] = row
      me._valdec[row] = val
      cur.close()
    return me._valenc[h]
  
  def _decode_val(me, cxn, row):
    if row not in me._valdec:
      cur = cxn.cursor()
      cur.execute('select val from valbag where rowid=?', (row,))
      val = valtool.unpack(cur.fetchone()[0], me._unpack_unwrap(cxn))
      me._valdec[row] = val
      me._valenc[_valhash(val)] = row
      cur.close()
    return me._valdec[row]
  
  @staticmethod
  def _chunkify(xs):
    szavg = 16
    while 8*szavg < len(xs):
      szavg *= 2
    
    def test(x):
      c = struct.unpack_from("<i", valtool.Hasher().eat(x).digest())[0]
      return c & szavg-1 == 0
    
    chks = [[]]
    for x in xs:
      if len(chks[-1]) > 0 and test(x):
        chks.append([])
      chks[-1].append(x)
    return chks
  
  _sizeof_int = struct.calcsize("<i")
  
  def _encode_query(me, cxn, done, qarg, legible):
    if done:
      return buffer(valtool.pack((0, me._encode_val(cxn, qarg))))
    else:
      ks = {_keykind_arg:[], _keykind_input:[]}
      for k,x in qarg:
        ks[k].append(x)
      for k in sorted(ks):
        ks[k].sort()
      t = (
        1 if legible else 2,
        me._encode_val(cxn, ks[_keykind_arg]),
        me._encode_val(cxn, ks[_keykind_input])
      )
      return buffer(valtool.pack(t))
  
  def _decode_query(me, cxn, s):
    s = valtool.unpack(s)
    if s[0] == 0:
      return (True, me._decode_val(cxn, s[1]), None)
    else:
      legible = s[0] == 1
      keys = set()
      for x in me._decode_val(cxn, s[1]):
        keys.add((_keykind_arg, x))
      for x in me._decode_val(cxn, s[2]):
        keys.add((_keykind_input, x))
      return (False, keys, legible)
  
  @staticmethod
  def _form_ans(legible, keyvals):
    ks = keyvals.keys()
    ks.sort()
    return (tuple if legible else _seqhash)(keyvals[k] for k in ks)
  
  def _split_ans(me, legible, ans):
    if legible:
      ans = valtool.pack(ans)
      a = struct.unpack_from("<i", valtool.Hasher().raw(ans).digest())[0]
      b = buffer(ans)
    else:
      a = struct.unpack_from("<i", ans)[0]
      b = buffer(ans, me._sizeof_int)
    return a, b
  
  @staticmethod
  def _merge_ans(legible, a, b):
    if legible:
      return valtool.unpack(b)
    else:
      return struct.pack("<i", a) + str(b)

  def memo_a(me, funh, view, calc_a):
    # return: _Log
    # calc_a: view,_Log -> result
    
    config = me._oven._config
    
    # check that we aren't subsumed by any wip
    disjoint = set() # set(_Log)
    if funh not in me._wips:
      me._wips[funh] = set()
    wips = me._wips[funh]
    
    while True:
      disjoint &= wips
      if len(disjoint) == len(wips): # disjoint with all wips
        acq = me._lock.acquire()
        new_wip = me._signew.begin_frame()
        yield async.Wait(acq)
        if not new_wip.aggregate():
          break
        else:
          me._lock.release()
      else: # test disjointness with wip
        wip = (wips - disjoint).pop()
        ix = 0
        while True:
          while ix == len(wip._seen):
            if wip._done:
              yield async.Result(wip)
            else:
              yield async.Wait(wip._bar.enlist())
          
          keys, h0 = wip._seen[ix]
          ys = yield async.Sync(view.keys_a(keys))
          ix += 1
          h1 = _seqhash((key,ys[key]) for key in keys)
          
          if h0 != h1:
            disjoint.add(wip)
            break
    
    # we have the lock, test the memo cache
    parent, keys, legible, ans = -1, None, False, funh
    log = _Log(funh) # build as we traverse trie
    while True:
      def look(cxn, parent, legible, ans):
        cur = cxn.cursor()
        ans_a, ans_b = me._split_ans(legible, ans)
        cur.execute(
          "select rowid, query " +
          "from logtrie " +
          "where parent=? and ans_a=? and ans_b=?",
          (parent, ans_a, ans_b))
        r = cur.fetchone()
        if r is None:
          return None
        cur.close()
        return (r[0],) + me._decode_query(cxn, r[1])
      
      got = yield async.Sync(me._oven._dbjob(
        lambda cxn: look(cxn, parent, legible, ans)
      ))
      if got is None:
        log = None
        break
      
      parent, done, qarg, legible = got
      if done:
        log.finish(qarg)
        break
      else:
        keys = qarg
        grp = config._keys_group(keys)
        log.add_bags((keys,))
        ys = yield async.Sync(view.keys_a(keys))
        ans = me._form_ans(legible, ys)
    
    if log is not None:
      me._lock.release()
    else: # must compute
      view = _View.fresh(view)
      log = _WipLog(funh)
      wips.add(log)
      me._signew.pulse(True) # signal new wip created
      me._lock.release()
      
      try:
        result = yield async.Sync(calc_a(view, log))
        log.finish(result)
      except Exception, e:
        log.explode(e, sys.exc_traceback)
      
      # done computing, put in cache
      class Thunk(object):
        def __init__(me, **kw):
          for x in kw:
            setattr(me, x, kw[x])
      
      box = Thunk(
        ix = 0,
        parent = -1,
        legible = False,
        ans = funh,
        redundant = None,
        missing = None
      )
      
      yield async.Wait(me._lock.acquire())
      wips.discard(log)
      
      def prefix(cxn):
        cur = cxn.cursor()
        box.redundant = False
        box.missing = frozenset()
        while True:
          ans_a, ans_b = me._split_ans(box.legible, box.ans)
          cur.execute(
            "select rowid, query " +
            "from logtrie " +
            "where parent=? and ans_a=? and ans_b=?",
            (box.parent, ans_a, ans_b))
          got = cur.fetchone()
          if got is None:
            return
          
          box.parent = got[0]
          done, keys, box.legible = me._decode_query(cxn, got[1])
          if done or box.ix == len(log._bags):
            box.redundant = True
            return
          
          box.missing = log.shift_bag(box.ix, keys)
          if len(box.missing) > 0:
            return
          box.ans = me._form_ans(box.legible, view.keys_memoed(keys))
          box.ix += 1
      
      while True:
        yield async.Sync(me._oven._dbjob(prefix))
        if len(box.missing) > 0:
          yield async.Sync(view.keys_a(box.missing))
        else:
          break
      
      if box.redundant:
        sys.stderr.write('WARNING: redundant computation detected!\n')
      else:
        def insert(cxn):
          cur = cxn.cursor()
          ok = False
          try:
            while box.ix <= len(log._bags):
              ans_a, ans_b = me._split_ans(box.legible, box.ans)
              if box.ix == len(log._bags):
                done = True
                qarg = log._result
              else:
                done = False
                qarg = log._bags[box.ix]
              qstr = me._encode_query(cxn, done, qarg, box.legible)
              cur.execute(
                "insert into logtrie(parent,ans_a,ans_b,query) " +
                "values(?,?,?,?)",
                (box.parent, ans_a, ans_b, qstr))
              box.parent = cur.lastrowid
              if not done:
                box.legible = config.group_legible(config._keys_group(qarg))
                box.ans = me._form_ans(box.legible, view.keys_memoed(qarg))
              box.ix += 1
            ok = True
          finally:
            if ok:
              cxn.commit()
            else:
              cxn.rollback()
        
        if log.error() is None:
          yield async.Sync(me._oven._dbjob(insert))
      
      me._lock.release()
    
    yield async.Result(log)

  if False: # search is totally out-of-date
    def search_a(me, funtest):
      def find(cxn, par, legible, test):
        cur = cxn.cursor()
        if test is TestNo:
          rows = ()
        elif isinstance(test, TestEqualAny):
          rows = []
          for ans in test.values:
            ans_a, ans_b = me._split_ans(legible, ans)
            cur.execute(
              "select rowid,ans_a,ans_b,query " +
              "from logtrie " +
              "where par=? and ans_a=? and ans_b=?",
              (par, ins_a, ans_b))
            rows += cur.fetchall()
        else:
          cur.execute(
            "select rowid,ans_a,ans_b,query " +
            "from logtrie " +
            "where par=?",
            (par,))
          rows = cur.fetchall()
        
        ans = []
        for row,ans_a,ans_b,query in rows:
          ans = me._merge_ans(legible, ans_a, ans_b)
          m = test(ans)
          if m is not MatchNone:
            done, qarg, legible = me._decode_query(cxn, query)
            ans.append((row, done, qarg, legible, m))
        return ans
      
      oven = me._oven
      query_a = oven.query_a
      
      def hashed_test(test):
        if type(test) is TestEqualAny:
          h2v = dict((_seqhash(v),v) for v in test.values)
          return TestEqualAny(h2v.keys(), lambda h: test(h2v[h]))
        elif type(test) is TestNotEqualAll:
          hs = tuple(_seqhash(v) for v in test.values)
          class T(tuple):
            def __getitem__(me, i):
              raise Exception("you should't be looking at this")
          return TestNotEqualAll(hs, lambda h: test(T()))
        else:
          assert False
            
      def cont_found_a(fut):
        # depth-first
        assert fut is find_futs[-1]
        find_futs.pop()
        
        if len(find_futs) > 0:
          futs[find_futs[0]] = (cont_found_a, find_futs[0])
        
        for row,done,qarg,legible,m in fut.result():
          if done:
            m.result(qarg)
          else:
            keys = qarg
            xs = {}
            for k,x in keys:
              if k not in xs: xs[k] = set()
              xs[k].add(x)
            
            if _keykind_arg in xs:
              xs[_keykind_arg] = sorted(xs[_keykind_arg])
              arg_test = m.args(xs[_keykind_arg])
              if not legible:
                arg_test = hashed_test(test)
            else:
              arg_test = None
            
            if _keykind_input in xs:
              xs[_keykind_input] = sorted(xs[_keykind_input])
              inp_test_fut = yield async.Begin(m.inputs_a(xs[_keykind_input], query_a))
            else:
              inp_test_fut = async.Future()
              inp_test_fut.finish(None)
            
            futs[fut] = (cont_test_a, arg_test, inp_test_fut, row, legible, keys)
      
      def cont_test_a(arg_test, inp_test_fut, row, legible, keys):
        inp_test = inp_test_fut.result()
        # here, combine tests arg & input
        # HARD! need to combine Matches
        fut = yield async.Begin(oven._dbjob(
          lambda cxn: find(cxn, row, legible, test)
        ))
        if len(find_futs) == 0:
          futs[fut] = (cont_found_a, fut)
        find_futs.append(fut)
      
      fut = yield async.Begin(oven._dbjob(
        lambda cxn: find(cxn, -1, False, hashed_test(funtest))
      ))
      find_futs = deque([fut])
      futs = {fut: (cont_found_a, fut)}
      
      while len(futs) > 0:
        fut = yield async.WaitAny(futs)
        cont = futs.pop(fut)
        yield async.Sync(cont[0](*cont[1:]))
