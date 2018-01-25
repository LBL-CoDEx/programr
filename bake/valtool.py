from array import array
import binascii
from collections import deque
import hashlib
import re
import struct
import sys
import zlib

_bin2hex = binascii.hexlify
_hex2bin = binascii.unhexlify

hash_zero = '\0'*16

def hash_sum(*xs):
  c = 0L
  for x in xs:
    c += long(_bin2hex(x), 16)
  c = '%x' % c
  if len(c) > 32:
    c = c[-32:]
  c = '0'*(32-len(c)) + c
  return _hex2bin(c)

def hash_subtract(a, b):
  a = long(_bin2hex(a), 16)
  b = long(_bin2hex(b), 16)
  c = a + (1L<<128) - b
  c = '%x' % c
  if len(c) > 32:
    c = c[-32:]
  c = '0'*(32-len(c)) + c
  return _hex2bin(c)

def follow(x):
  def g(f):
    if not hasattr(f, '__valtool_follow__'):
      setattr(f,'__valtool_follow__',[])
    getattr(f,'__valtool_follow__').append(x)
    return f
  return g

class Hasher(object):
  def _make():
    act = {}
    
    def f(put,s,x):
      code = x.func_code
      cells = x.func_closure or ()
      follow = getattr(x, '__valtool_follow__', ())
      put('fn.%x.%x.%x.%x.' % (len(code.co_code), len(code.co_consts or ()), len(cells), len(follow)))
      put(code.co_code)
      s += code.co_consts or ()
      for cell in cells:
        s.append(cell.cell_contents)
      s += follow
    act[type(f)] = f
    
    def f(put,s,x):
      put('ls.%x.' % len(x))
      s += x
    act[list] = f

    def f(put,s,x):
      put('tp.%x.' % len(x))
      s += x
    act[tuple] = f
    
    def f(put,s,x):
      put('d.%x.' % len(x))
      for k,v in sorted(x.iteritems()):
        s.append(k)
        s.append(v)
    act[dict] = f
    
    def f(put,s,x):
      put('se.%x.' % len(x))
      s += sorted(x)
    act[set] = f
    
    def f(put,s,x):
      put('fs.%x.' % len(x))
      s += sorted(x)
    act[frozenset] = f
    
    def f(put,s,x):
      put('sz.%x.' % len(x))
      put(x)
    act[str] = f
    
    def f(put,s,x):
      put('by.%x.' % len(x))
      put(x)
    act[bytearray] = f
    
    def f(put,s,x):
      put('ar.%s.%x.' % (x.typecode, len(x)))
      put(buffer(x))
    act[array] = f
    
    def f(put,s,x):
      put('bu.%x.' % len(x))
      put(x)
    act[buffer] = f
    
    def f(put,s,x):
      put('i.%x.' % long(x))
    act[int] = f
    act[long] = f
    
    def f(put,s,x):
      put('fo.')
      put(struct.pack('<d', x))
    act[float] = f
    
    def f(put,s,x):
      put('t.' if x else 'f.')
    act[bool] = f
    
    def f(put,s,x):
      put('n.')
    act[type(None)] = f
    
    def act_unk(put,s,x):
      ty = type(x)
      if ty is getattr(sys.modules[ty.__module__], ty.__name__, None):
        if hasattr(x, '__getstate__'):
          put('os.%s.%s.' % (ty.__module__, ty.__name__))
          s.append(x.__getstate__())
        else:
          fs = getattr(ty,'__slots__',None) or getattr(x,'__dict__',{}).iterkeys()
          fs = list(f for f in sorted(fs) if hasattr(x, f))
          put('of.%s.%s.%x.' % (ty.__module__, ty.__name__, len(fs)))
          for f in fs:
            s.append(f)
            s.append(getattr(x, f))
      else:
        put('?')
    
    return lambda ty: act.get(ty, act_unk)
  
  _act = (_make(),) # hide it from class
  
  def __init__(me, that=None):
    if that is None:
      me._h, me._dig = hashlib.md5(), None
    else:
      me._h, me._dig = that._h.copy(), that._dig
  
  def raw(me, x):
    if x is not None:
      me._h.update(buffer(x))
      me._dig = None
    return me
  
  def eatseq(me, xs):
    for x in xs:
      me.eat(x)
    return me
  
  def eat(me, x):
    act = me._act[0]
    b = bytearray()
    s = [x] # stack of unprocessed values
    open_x = []
    open_b0 = array('i')
    open_h = []
    open_num = array('i')
    open_set = {}
    memo = {}
    xc = 0
    
    def put(z):
      h = open_h[-1]
      b0 = open_b0[-1]
      if h is not None:
        h.update(buffer(z))
      elif len(b)-b0 + len(z) >= 256:
        h = hashlib.md5()
        h.update(buffer(b, b0))
        h.update(buffer(z))
        del b[b0:]
        open_h[-1] = h
      else:
        b.extend(z)
    
    while len(s) > 0:
      x = s.pop()
      open_h.append(None)
      open_b0.append(len(b))
      if not getattr(x, '__valtool_ignore__', False):
        id_x = id(x)
        if id_x in open_set:
          put('cy.%x.' % open_set[id_x])
          sn = 0
        elif id_x in memo:
          put(memo[id_x])
          sn = 0
        else:
          sn = len(s)
          act(type(x))(put, s, x)
          sn = len(s) - sn
      else:
        put('_')
        sn = 0
      
      if sn > 0:
        open_x.append(x)
        open_num.append(sn)
        open_set[id(x)] = xc
        xc += 1
      else:
        while True:
          if len(open_num) == 0:
            assert len(s) == 0
            break
          
          h = open_h.pop()
          b0 = open_b0.pop()
          if h is not None:
            assert b0 == len(b)
            dig = '#' + h.digest()
            memo[id(x)] = dig
            put(dig)
          else:
            h = open_h[-1]
            b0 = open_b0[-1]
            if h is not None:
              h.update(buffer(b, b0))
              del b[b0:]
            elif len(b)-b0 >= 256:
              h = hashlib.md5()
              h.update(buffer(b, b0))
              del b[b0:]
              open_h[-1] = h
          
          sn = open_num[-1] - 1
          if sn > 0:
            open_num[-1] = sn
            break
          else:
            open_num.pop()
            x = open_x.pop()
            del open_set[id(x)]
    
    assert len(open_h) == 1
    assert 0 == open_b0[0]
    
    h = open_h[0]
    if h is not None:
      me._h.update('#' + h.digest())
    else:
      me._h.update(buffer(b))
    
    me._dig = None
    return me
    
  def digest(me):
    if me._dig is None:
      me._dig = me._h.digest()
    return me._dig

if False: # hasher test
  import time
  val = lambda: dict((a,str(666*a)*7) for a in xrange(1<<10))
  x = val()
  x1 = tuple(x for i in xrange(1<<10))
  x2 = tuple(val() for i in xrange(1<<10))
  t1 = time.clock()
  h1 = Hasher().eat(x1).digest()
  t2 = time.clock()
  t1 = t2 - t1
  h2 = Hasher().eat(x2).digest()
  t2 = time.clock() - t2
  print 'x1==x2',  h1==h2
  print 't2/t1', t2*1.0/t1
  
def _make():
  cata_tbl = {} # maps types to catamorphism actions
  ana_tbl = [] # maps leading bytes (opcodes) to anamorphism actions
  # during _make, len(ana_tbl) represents the next free opcode
  
  def dump(s):
    if False:
      print>>sys.stderr, len(ana_tbl), '=', s
  
  identity = lambda x: x
  
  def putnat(n, b):
    while True:
      c = n & 0x7f
      n >>= 7
      c = c | (0x80 if n != 0 else 0)
      b.append(c)
      if n == 0: break
  
  def takenat(b, p):
    n = 0
    i = 0
    while True:
      c = ord(b[p+i])
      n += (c & 0x7f) << (7*i)
      i += 1
      if (c & 0x80) == 0: break
    return n, p+i
  
  # None,True,False
  def make():
    op_n = len(ana_tbl)
    op_t = op_n+1
    op_f = op_n+2
    mc = {None:op_n, True:op_t, False:op_f}
    def cact(x, b, cata):
      b.append(mc[x])
    def aact(op, b, p, ana):
      return (None, True, False)[op-op_n], p
    cata_tbl[type(None)] = cact
    cata_tbl[bool] = cact
    dump('none,true,false')
    ana_tbl.extend([aact]*3)
  make()
  
  # int,long: distinction not preserved, 1L will be treated as int(1)
  def make():
    op0 = len(ana_tbl)
    lb, ub = -32, 96
    op_pos = op0 + ub-lb
    op_neg = op0 + ub-lb + 1
    def cact(x, b, cata):
      if x < lb:
        b.append(op_neg)
        putnat(lb-1 - x, b)
      elif ub <= x:
        b.append(op_pos)
        putnat(x - ub, b)
      else:
        b.append(op0 + x - lb)
    def aact(op, b, p, ana):
      if op < op_pos:
        return op - op0 + lb, p
      else:
        n, p = takenat(b, p)
        n = ub + n if op == op_pos else lb-1 - n
        return n, p
    cata_tbl[int] = cact
    cata_tbl[long] = cact
    dump('int,long')
    ana_tbl.extend([aact]*(ub-lb + 2))
  make()
  
  # float
  def make():
    op_n0 = len(ana_tbl)
    sz = struct.calcsize('>d')
    def cact(x, b, cata):
      s = struct.pack('>d', x)
      s = s.rstrip('\x00')
      b.append(op_n0 + len(s))
      b.extend(s)
    def aact(op, b, p, ana):
      n = op - op_n0
      s = buffer(b, p, n)
      p += n
      s = str(s + '\x00'*(sz - n))
      x = struct.unpack('>d', s)[0]
      return x, p
    cata_tbl[float] = cact
    ana_tbl.extend([aact]*(sz+1))
  make()
  
  # string,bytearray,buffer
  def make(ty,small):
    op0 = len(ana_tbl)
    def cact(x, b, cata):
      if len(x) < small:
        b.append(op0 + len(x))
      else:
        b.append(op0 + small)
        putnat(len(x) - small, b)
      b.extend(x)
    def aact(op, b, p, ana):
      op -= op0
      if op < small:
        n = op
      else:
        n, p = takenat(b, p)
        n += small
      return ty(buffer(b, p, n)), p + n
    cata_tbl[ty] = cact
    dump(repr(ty))
    ana_tbl.extend([aact]*(small + 1))
  make(str, 16)
  make(bytearray, 4)
  make(buffer, 0)
  
  # list,tuple,dict,set,frozenset,deque
  def make(ty, items, small):
    op0 = len(ana_tbl)
    def cact(x, b, cata):
      if len(x) < small:
        b.append(op0 + len(x))
      else:
        b.append(op0 + small)
        putnat(len(x) - small, b)
      for x1 in items(x):
        cata(x1)
    def aact(op, b, p, ana):
      op -= op0
      if op < small:
        n = op
      else:
        n, p = takenat(b, p)
        n += small
      xs = []
      for i in xrange(n):
        x, p = ana(b, p)
        xs.append(x)
      return (xs if ty is list else ty(xs)), p
    cata_tbl[ty] = cact
    dump(repr(ty))
    ana_tbl.extend([aact]*(small + 1))
  make(tuple, identity, 8)
  make(list, identity, 4)
  make(dict, lambda xs: sorted(xs.iteritems()), 4)
  make(set, sorted, 4)
  make(frozenset, sorted, 4)
  make(deque, identity, 0)
  
  # array
  def make():
    op = len(ana_tbl)
    def cact(x, b, cata):
      b.append(op)
      b.append(x.typecode)
      putnat(len(x), b)
      for x1 in x:
        cata(x1)
    def aact(op, b, p, ana):
      tc = b[p]
      n, p = takenat(b, p+1)
      a = array(tc)
      for i in xrange(n):
        x1, p = ana(b, p)
        a.append(x1)
      return a, p
    cata_tbl[array] = cact
    dump('array')
    ana_tbl.append(aact)
  make()
  
  # unknown object
  def make():
    op_st = len(ana_tbl)
    op_fs = len(ana_tbl) + 1
    def cact(x, b, cata):
      ty = type(x)
      mod = ty.__module__
      cls = ty.__name__
      assert ty is getattr(sys.modules[mod], cls, None)
      if hasattr(x, '__getstate__'):
        b.append(op_st)
        cata((mod,cls))
        cata(x.__getstate__())
      else:
        b.append(op_fs)
        fs = getattr(ty, '__slots__', None) or getattr(x,'__dict__',{}).iterkeys()
        fs = tuple(f for f in sorted(fs) if hasattr(x, f))
        cata((mod,cls) + fs)
        for f in fs:
          cata(getattr(x, f))
    def aact(op, b, p, ana):
      if op == op_st:
        (mod, cls), p = ana(b, p)
        x = getattr(sys.modules[mod], cls)()
        st, p = ana(b, p)
        x.__setstate__(st)
      else:
        tup, p = ana(b, p)
        mod, cls, flds = tup[0], tup[1], tup[2:]
        x = getattr(sys.modules[mod], cls)()
        for f in flds:
          val, p = ana(b, p)
          setattr(x, f, val)
      return x, p
    # cata_tbl has no entries because type is unknown
    dump('unk')
    ana_tbl.extend([aact]*2)
    return cact
  unk_cact = make()
  
  wrap_tag_n = 8
  wrap_aact = object()
  wrap_op0 = len(ana_tbl)
  dump('wrap')
  ana_tbl.extend([wrap_aact]*wrap_tag_n)
  
  ana_tbl.extend([None]*(255-len(ana_tbl)))
    
  def zlib_aact(op, b, p, ana):
    s = zlib.decompress(buffer(b, p))
    return ana(buffer(s), 0)
  zlib_op = len(ana_tbl) # 255
  ana_tbl.append(zlib_aact)
  
  assert len(ana_tbl) <= 256
  #print 'OPCODES:', len(ana_tbl)
  
  def pack(x, control=None):
    b = bytearray()
    
    def reset(st):
      off = st
      del b[off:]
      
    def putx(x, cata, st):
      reset(st)
      cata_tbl.get(type(x), unk_cact)(x, b, cata)
      return len(b) - st
    
    def putwrap(ctrl, tag, comps, st):
      assert 0 <= tag and tag < wrap_tag_n
      reset(st)
      b.append(wrap_op0 + tag)
      for c in comps:
        cata(c, ctrl)
      return len(b) - st
    
    def deft_control(x, cata, wrap):
      cata(x, deft_control)
    
    def cata(x, control=None):
      assert not getattr(x, '__valtool_ignore__', False)
      control = deft_control if control is None else control
      st = len(b)
      control(
        x,
        lambda x,ctrl=None: putx(x, lambda x: cata(x,ctrl), st),
        lambda ctrl,tag,*comps: putwrap(ctrl, tag, comps, st)
      )
    
    cata(x, control)
    
    z = zlib.compress(buffer(b), 9)
    if len(z)+1 < len(b):
      return bytearray([zlib_op]) + bytearray(z)
    else:
      return b
  
  class Thunk(object):
    def __init__(me, **kw):
      for x in kw:
        setattr(me, x, kw[x])
  
  def make_ana(unwrap):
    def ana(b, _p):
      box = Thunk(p=_p)
      op = ord(b[box.p])
      box.p += 1
      act = ana_tbl[op]
      if act is wrap_aact:
        def getcomps(n):
          ans = []
          for i in xrange(n):
            x, box.p = ana(b, box.p)
            ans.append(x)
          return ans
        ans = unwrap(op - wrap_op0, getcomps)
      else:
        #sys.stderr.write('OP {}\n'.format(op))
        ans, box.p = act(op, b, box.p, ana)
      return ans, box.p
    return ana
  
  def unpack(b, unwrap=None):
    """returns reconstructed value beginning at string/buffer b."""
    return make_ana(unwrap)(buffer(b), 0)[0]
  
  def unpack_one(b, unwrap=None):
    """returns tuple (val,n) containing unpacked value and the length
    it occupied in the buffer."""
    return make_ana(unwrap)(buffer(b), 0)
  
  return pack, unpack

pack, unpack = _make()

if False:
  def ctrl1(x, cata, wrap):
    wrap(ctrl0, 0, x, x)
  def ctrl0(x, cata, wrap):
    cata(x, ctrl1)
  def unwrap(tag, comps):
    return comps(2)[0]
  
  x = [4 for x in range(3)]
  x=(x,x)
  print x
  s = pack(x, ctrl1)
  print [c for c in s]
  print unpack(s, unwrap)

if False: # pack/unpack test
  x = {
    (1,2): "hello",
    (-1,1<<31): bytearray('ba'),
    "": frozenset([9,8,2]),
    None: array('i',range(4))
  }  
  print unpack(pack(x))
  print 'equal:', x == unpack(pack(x))
  
  class Class1(object):
    def __init__(me,x):
      me.a = x
      me.hello = 'world'
      me.hell = 666
  class Class2(object):
    __slots__ = ('__a', 'b')
    def __init__(me):
      me.__a = 5
  class Class3(object):
    def __getstate__(me):
      return frozenset('yay')
    def __setstate__(me, x):
      assert x == frozenset('yay')
  
  print pack((Class1('ONE'),Class1('TWO')))
  
  x = unpack(pack(Class2()))
  print x._Class2__a, hasattr(x,'b')
  
  x = unpack(pack(Class3()))
