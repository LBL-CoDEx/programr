import threading
import multiprocessing
import types
from collections import deque
import sys
import weakref

catch_exceptions = True # set to False when debugging

default_pool_size = multiprocessing.cpu_count()

class Pool(object):
  def __init__(me, size=default_pool_size):
    me.size = size

def pool_size(pool):
  return default_pool_size if pool is None else pool.size

def assign_pool(pool):
  def lam(f):
    f.async_pool = pool
    return f
  return lam

def assigned_pool(lam):
  return lam.async_pool if hasattr(lam, 'async_pool') else None

class _Return(object):
  pass

class Result(_Return):
  __slots__ = ('_val',)
  def __init__(me, val):
    me._val = val
  def __call__(me):
    return me._val

class Raise(_Return):
  __slots__ = ('_ex','_tb')
  def __init__(me, ex, tb):
    me._ex = ex
    me._tb = tb
  def __call__(me):
    raise type(me._ex), me._ex, me._tb

class _Yield(object):
  pass

class Begin(_Yield):
  __slots__ = ('_task','_fut_rcvr')
  def __init__(me, task, future_receiver=None):
    me._task = task
    me._fut_rcvr = future_receiver

class Sync(_Yield):
  __slots__ = ('_task',)
  def __init__(me, task):
    assert not isinstance(task, _Future)
    me._task = task

class Wait(_Yield):
  __slots__ = ('_fut',)
  def __init__(me, fut):
    assert isinstance(fut, _Future)
    me._fut = fut

class WaitAny(_Yield):
  __slots__ = ('_futs',)
  def __init__(me, futs):
    me._futs = futs

class _Future(object):
  def __init__(me):
    me._wait_sts = set() # set of waiting ItStat's
    me._ret = None # _Return
  
  def done(me):
    return me._ret is not None
  
  def result(me):
    assert me._ret is not None
    return me._ret()

  def _finish(me, ret):
    assert isinstance(ret, _Return)
    assert me._ret is None
    me._ret = ret
    for st in me._wait_sts:
      for fut in st.wait_futs:
        if fut is not me:
          assert st in fut._wait_sts
          fut._wait_sts.discard(st)
      st.wait_futs = None
      st.wake_fut = me
      st.wake()
    me._wait_sts.clear()
  
  def __getstate__(me):
    assert me._ret is not None
    return me._ret

class Future(_Future):
  def __init__(me):
    super(Future, me).__init__()
  
  def finish(me, value=None):
    me._finish(Result(value))
  
  def explode(me, ex, tb=None):
    me._finish(Raise(type(ex), ex, tb))


def run(a):
  deft_pool = Pool(size=default_pool_size)
  class PoolSt(object):
    __slots__ = ('ref_pool','num','idle','cv','jobs')
    def __init__(me, pool):
      me.ref_pool = weakref.ref(pool)
      me.num = 0
      me.idle = 0
      me.cv = threading.Condition(lock)
      me.jobs = deque()
  
  lock = threading.Lock()
  cv_main = threading.Condition(lock)
  pool_sts = weakref.WeakKeyDictionary()
  wake_list = deque() # deque(ItStat) -- queue of iters to wake, no duplicates
  wake_set = set() # set(ItStat) -- set of iters to wake, matches wake_list
  class closure:
    jobs_notdone = 0
    quitting = False
  
  def post_job(pool, fut, job):
    pool = pool or deft_pool
    poo = pool_sts.get(pool, None)
    if poo is None or (poo.idle <= 0 and poo.num < pool.size):
      if poo is None:
        pool_sts[pool] = poo = PoolSt(pool)
      poo.num += 1
      poo.idle += 1
      threading.Thread(target=worker_proc, kwargs={'pool_st':poo}).start()
    poo.idle -= 1
    closure.jobs_notdone += 1
    poo.jobs.append((fut, job))
    poo.cv.notify()
  
  def worker_proc(*args, **kwargs):
    poo = kwargs['pool_st']
    if poo.ref_pool() is None: return
    lock.acquire()
    while True:
      while not closure.quitting and len(poo.jobs) > 0:
        while not closure.quitting and len(poo.jobs) > 0:
          fut, job = poo.jobs.popleft()
          lock.release()
          try:
            ret = Result(job())
          except Exception, ex:
            ret = Raise(ex, sys.exc_traceback)
          lock.acquire()
          poo.idle += 1
          closure.jobs_notdone -= 1
          fut._finish(ret)
          if len(wake_list) > 0 and len(poo.jobs) > 0:
            # tell the main thread it should do the awakening, we have more jobs to do
            cv_main.notify()
        # since we're out of jobs we might as well wake up some iterators
        awaken()
      if closure.quitting: break
      if poo.ref_pool() is None: break
      poo.cv.wait()
    lock.release()
  
  class ItStat(object):
    __slots__ = (
      'trace', # deque((file,name,line)) -- most recent stack of iterators
      'par_fut', # _Future -- future representing the result of this iterator
      'it', # iterator routine
      'wait_futs', # tuple(_Future) -- futures this suspended iterator is waiting on
      'wake_meth', # lambda ItStat: _Yield -- how to awaken suspended iterator
      'wake_fut', # future with which to wake iterator
    )
    
    def __init__(me, par_st, par_fut, it):
      assert par_st is None or isinstance(par_st, ItStat)
      assert isinstance(par_fut, _Future)
      me.par_fut = par_fut
      me.it = it
      me.wait_futs = None
      me.wake_meth = None
      me.wake_fut = None
      if par_st is not None:
        me.trace = list(par_st.trace[1 if len(par_st.trace)>=50 else 0:])
        me.trace.append((
          par_st.it.gi_code.co_filename,
          par_st.it.gi_code.co_name,
          par_st.it.gi_frame.f_lineno if par_st.it.gi_frame is not None else None
        ))
      else:
        me.trace = ()
    
    def wake(me):
      if me not in wake_set:
        wake_list.append(me)
        wake_set.add(me)
    
    def traceback(me):
      lines = ['Asynchronous traceback (most recent call last):']
      if len(me.trace) >= 50:
        lines.append('  ...')
      fmt = '  File "%s", line %s, in %s'
      for file,name,lno in me.trace:
        lno = '?' if lno is None else str(lno)
        lines.append(fmt % (file, lno, name))
      lno = '?' if me.it.gi_frame is None else str(me.it.gi_frame.f_lineno)
      lines.append(fmt % (me.it.gi_code.co_filename, lno, me.it.gi_code.co_name))
      return '\n'.join(lines)
  
  send_none = lambda st: st.it.send(None)
  send_fut = lambda st: st.it.send(st.wake_fut)
  def send_result(st):
    ret = st.wake_fut._ret
    if type(ret) is Result:
      return st.it.send(ret._val)
    elif type(ret) is Raise:
      return st.it.throw(type(ret._ex), ret._ex, ret._tb)
    else:
      assert False
  
  def begin(par_st, task):
    fut = _Future()
    if isinstance(task, types.GeneratorType):
      st = ItStat(par_st, fut, task.__iter__())
      wake_list.appendleft(st)
      wake_set.add(st)
      st.wake_meth = send_none
    elif callable(task):
      pool = assigned_pool(task)
      post_job(pool, fut, task)
    else:
      assert False
    return fut
  
  def awaken():
    # just keep pulling iterators off the wake_list until she's empty
    while len(wake_list) > 0:
      st = wake_list.popleft()
      wake_set.discard(st)
      assert st.wait_futs is None
      
      if catch_exceptions:
        try:
          got = st.wake_meth(st)
        except StopIteration:
          got = Result(None)
        except AssertionError:
          raise
        except Exception, ex:
          if not hasattr(ex, 'async_traceback'):
            ex.async_traceback = st.traceback()
          got = Raise(ex, sys.exc_traceback)
      else:
        try:
          got = st.wake_meth(st)
        except StopIteration:
          got = Result(None)
      
      if isinstance(got, _Return):
        st.par_fut._finish(got)
      elif isinstance(got, Begin):
        assert st not in wake_set
        wake_list.appendleft(st)
        wake_set.add(st)
        fut = begin(st, got._task)
        if got._fut_rcvr is not None:
          got._fut_rcvr(fut)
        st.wake_fut = fut
        st.wake_meth = send_fut
      elif isinstance(got, Sync):
        fut = begin(st, got._task)
        st.wait_futs = (fut,)
        assert st not in fut._wait_sts
        fut._wait_sts.add(st)
        st.wake_meth = send_result
      elif isinstance(got, Wait):
        fut = got._fut
        if fut._ret is not None: # done
          assert st not in wake_set
          wake_list.appendleft(st)
          wake_set.add(st)
          st.wake_fut = fut
        else:
          st.wait_futs = (fut,)
          assert st not in fut._wait_sts
          fut._wait_sts.add(st)
        st.wake_meth = send_result
      elif isinstance(got, WaitAny):
        futs = tuple(got._futs)
        empty = True
        for fut in futs: # test if any futures are already done, no need to sleep
          assert isinstance(fut, _Future)
          empty = False
          if fut._ret is not None: # done
            assert st not in wake_set
            wake_list.appendleft(st)
            wake_set.add(st)
            st.wake_fut = fut
            break
        assert not empty # waiting on the empty set means wait forever, we'll assume the user goofed
        if st not in wake_set: # none were already done, suspend st
          st.wait_futs = futs
          for fut in st.wait_futs:
            assert st not in fut._wait_sts
            fut._wait_sts.add(st)
        st.wake_meth = send_fut
      else:
        assert False
    
    if closure.jobs_notdone == 0:
      closure.quitting = True
      cv_main.notify()
      for poo in pool_sts.values():
        poo.cv.notify_all()
  
  lock.acquire()
  top = begin(None, a)
  while not closure.quitting:
    awaken()
    if closure.quitting: break
    try:
      cv_main.wait()
    except:
      closure.quitting = True
      for poo in pool_sts.values():
        poo.cv.notify_all()
      lock.release()
      raise
  lock.release()
  return top.result()

class Lock(object):
  def __init__(me):
    me._futs = None # if not-none then lock is taken
  
  def acquire(me):
    fut = Future()
    if me._futs is None:
      me._futs = deque()
      fut.finish()
    else:
      me._futs.append(fut)
    return fut
  
  def release(me):
    if len(me._futs) > 0:
      me._futs.popleft().finish()
    else:
      me._futs = None

class Barrier(object):
  def __init__(me):
    me._futs = deque()
  
  def enlist(me):
    fut = Future()
    me._futs.append(fut)
    return fut
  
  def fireone(me):
    if len(me._futs) > 0:
      me._futs.popleft().finish()
  
  def fireall(me):
    for fut in me._futs:
      fut.finish()
    me._futs.clear()

class _Frame(object):
  def __init__(me, seed):
    me._acc = seed
  def aggregate(me):
    return me._acc

class Signal(object):
  def __init__(me, seed=None, fold=lambda a,b: b):
    me._seed = seed
    me._fold = fold
    me._frames = weakref.WeakKeyDictionary()
    me._frame_reuse = None
  
  def begin_frame(me):
    if me._frame_reuse is None:
      frame = _Frame(me._seed)
      me._frames[frame] = None
      me._frame_reuse = frame
    return me._frame_reuse
  
  def pulse(me, x):
    me._frame_reuse = None
    for frame in me._frames:
      frame._acc = me._fold(frame._acc, x)

if False: # test code
  import urllib2
  import time
  def main_a():
    def get(url):
      print '> ' + url
      f = urllib2.urlopen(url)
      s = f.read(100)
      time.sleep(1)
      print '< ' + url
      return s
    urls = ['http://www.google.com','http://www.yahoo.com','http://www.microsoft.com']
    fut = {}
    for u in urls:
      fut[u] = yield Begin((lambda u: lambda: get(u))(u))
    for u in urls:
      f = yield WaitAny([fut[u]])
      print 'url: ' + u
    print 'done'
  def b():
    for i in range(2):
      try:
        yield Sync(main_a())
      except Exception, e:
        print 'caught ', e
  run(b())
