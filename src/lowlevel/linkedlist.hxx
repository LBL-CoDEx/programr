#ifndef _24be2124_962e_43d5_a7e7_1cd7485dd4bc
#define _24be2124_962e_43d5_a7e7_1cd7485dd4bc

namespace programr {
  template<class T>
  struct Links {
    T *prev=0x0; // reverse direction is a circular list: head->prev == tail
    T *next; // non-circular: tail->next == 0x0
    
    bool is_linked() const {
      return prev != 0x0;
    }
  };
  
  /* Instrusive linked list. Instances of T must explicitly declare members
   * of type Links<T> for use by LinkedList's. This allows a T to be an
   * element multiple lists. This class does no memory management of its
   * own, so its up to the user to make sure nodes get their memory freed.
   * 
   * To iterate a list:
   *   for(T *p = list.head(); p; p = (p->*list.links).next)
   *     ...
   * 
   * To iterate a list while emptying it:
   *   for(T *p; (p = list.pop_head());)
   *     ...
   */
  template<class T>
  class LinkedList {
  public:
    Links<T> T::* const links;
  private:
    T *_head;
  public:
    LinkedList(Links<T> T::*links):
      links(links),
      _head(0x0) {
    }
    
    T* head() const { return _head; }
    T* tail() const { return _head ? (_head->*links).prev : 0x0; }
    T* tail_unsafe() const { return (_head->*links).prev; }
    
    void push_head(T *t);
    void push_tail(T *t);
    T* pop_head();
    T* pop_tail();
    void remove(T *t);
    
    void clear() { // simply sets head=null, doesn't touch any nodes
      _head = 0x0;
    }
  };

  //////////////////////////////////////////////////////////////////////
  template<class T>
  void LinkedList<T>::push_head(T *t) {
    if(_head != 0x0) {
      (t->*links).next = _head;
      (t->*links).prev = (_head->*links).prev;
      (_head->*links).prev = t;
    }
    else {
      (t->*links).next = 0x0;
      (t->*links).prev = t;
    }
    _head = t;
  }
  
  template<class T>
  void LinkedList<T>::push_tail(T *t) {
    if(_head != 0x0) {
      (t->*links).next = 0x0;
      (t->*links).prev = (_head->*links).prev;
      ((_head->*links).prev->*links).next = t;
      (_head->*links).prev = t;
    }
    else {
      (t->*links).next = 0x0;
      (t->*links).prev = t;
      _head = t;
    }
  }

  template<class T>
  T* LinkedList<T>::pop_head() {
    T *t = _head;
    if(t) {
      _head = (t->*links).next;
      if(_head)
        (_head->*links).prev = (t->*links).prev;
      (t->*links).prev = 0x0; // mark as unlinked
    }
    return t;
  }
  
  template<class T>
  T* LinkedList<T>::pop_tail() {
    T *t = _head;
    if(t) {
      t = (t->*links).prev;
      if(t == _head)
        _head = 0x0;
      else {
        (_head->*links).prev = (t->*links).prev;
        ((t->*links).prev->*links).next = 0x0;
      }
      (t->*links).prev = 0x0; // mark as unlinked
    }
    return t;
  }
  
  template<class T>
  void LinkedList<T>::remove(T *t) {
    if((t->*links).next)
      ((t->*links).next->*links).prev = (t->*links).prev;
    else
      (_head->*links).prev = (t->*links).prev;
    
    if(t != _head)
      ((t->*links).prev->*links).next = (t->*links).next;
    else
      _head = (t->*links).next;
    
    (t->*links).prev = 0x0; // mark as unlinked
  }
}

#endif
