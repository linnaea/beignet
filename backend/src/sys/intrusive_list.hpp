/*
 * Copyright (c) 2007 Maciej Sinilo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef __GBE_INTRUSIVE_LIST_HPP__
#define __GBE_INTRUSIVE_LIST_HPP__

#include <vector>
#include "sys/platform.hpp"

namespace gbe
{
  /*! List elements must inherit from it */
  struct intrusive_list_node
  {
    INLINE intrusive_list_node() { next = prev = this; }
    INLINE bool in_list() const  { return this != next; }
    intrusive_list_node *next;
    intrusive_list_node *prev;
  };

  /*! Insert node such that prev -> node */
  void append(intrusive_list_node *node, intrusive_list_node *prev);
  /*! Insert node such that node -> next */
  void prepend(intrusive_list_node *node, intrusive_list_node *next);
  /*! Same as prepend */
  void link(intrusive_list_node* node, intrusive_list_node* nextNode);
  /*! Remove the node from its current list */
  void unlink(intrusive_list_node* node);

  template<typename Pointer, typename Reference>
  class intrusive_list_iterator
  {
  public:
    typedef Pointer pointer;
    typedef Reference reference;

    INLINE intrusive_list_iterator(): m_node(0) {}
    INLINE intrusive_list_iterator(Pointer iterNode) : m_node(iterNode) {}

    INLINE Reference operator*() const {
      GBE_ASSERT(m_node);
      return *m_node;
    }
    INLINE Pointer operator->() const { return m_node; }
    INLINE Pointer node() const { return m_node; }

    INLINE intrusive_list_iterator& operator++() {
      m_node = static_cast<Pointer>(m_node->next);
      return *this;
    }
    INLINE intrusive_list_iterator& operator--() {
      m_node = static_cast<Pointer>(m_node->prev);
      return *this;
    }
    INLINE intrusive_list_iterator operator++(int) {
      intrusive_list_iterator copy(*this);
      ++(*this);
      return copy;
    }
    INLINE intrusive_list_iterator operator--(int) {
      intrusive_list_iterator copy(*this);
      --(*this);
      return copy;
    }

    INLINE bool operator== (const intrusive_list_iterator& rhs) const {
      return rhs.m_node == m_node;
    }
    INLINE bool operator!= (const intrusive_list_iterator& rhs) const {
      return !(rhs == *this);
    }
  private:
    Pointer m_node;
  };

  class intrusive_list_base
  {
  public:
    typedef size_t size_type;

    INLINE void pop_back() { unlink(m_root.prev); }
    INLINE void pop_front() { unlink(m_root.next); }
    INLINE bool empty() const  { return !m_root.in_list(); }
    size_type size() const;

  protected:
    intrusive_list_base();
    INLINE ~intrusive_list_base() {}

    intrusive_list_node m_root;

  private:
    intrusive_list_base(const intrusive_list_base&);
    intrusive_list_base& operator=(const intrusive_list_base&);
  };

  template<class T>
  class intrusive_list : public intrusive_list_base
  {
  public:
    typedef T node_type;
    typedef T value_type;
    typedef intrusive_list_iterator<T*, T&> iterator;
    typedef intrusive_list_iterator<const T*, const T&> const_iterator;

    intrusive_list() : intrusive_list_base() {
      intrusive_list_node* testNode((T*)0);
      static_cast<void>(sizeof(testNode));
    }

    void push_back(value_type* v) { link(v, &m_root); }
    void push_front(value_type* v) { link(v, m_root.next); }

    iterator begin()  { return iterator(upcast(m_root.next)); }
    iterator end()    { return iterator(upcast(&m_root)); }
    iterator rbegin() { return iterator(upcast(m_root.prev)); }
    iterator rend()   { return iterator(upcast(&m_root)); }
    const_iterator begin() const  { return const_iterator(upcast(m_root.next)); }
    const_iterator end() const    { return const_iterator(upcast(&m_root)); }
    const_iterator rbegin() const { return const_iterator(upcast(m_root.prev)); }
    const_iterator rend() const   { return const_iterator(upcast(&m_root)); }

    INLINE value_type* front() { return upcast(m_root.next); }
    INLINE value_type* back()  { return upcast(m_root.prev); }
    INLINE const value_type* front() const { return upcast(m_root.next); }
    INLINE const value_type* back() const  { return upcast(m_root.prev); }

    value_type* MAYBE_UNUSED operator[](size_type n) {
      intrusive_list_node* iter = m_root.next;
      while(n) {
        iter = iter->next;
        --n;
      }

      return upcast(iter);
    }

    value_type* MAYBE_UNUSED at(size_type n) {
      intrusive_list_node* iter = m_root.next;
      while(n && iter != &m_root) {
        iter = iter->next;
        --n;
      }

      if(iter == &m_root)
        throw std::out_of_range("n");

      return upcast(iter);
    }

    std::vector<T*> MAYBE_UNUSED to_vector() {
      std::vector<T*> v;
      for(auto iter = m_root.next; iter != &m_root; iter = iter->next) {
        v.emplace_back(upcast(iter));
      }

      return v;
    }

    iterator insert(iterator pos, value_type* v) {
      link(v, pos.node());
      return iterator(v);
    }
    iterator erase(iterator it) {
      iterator itErase(it);
      ++it;
      unlink(itErase.node());
      return it;
    }
    iterator erase(iterator first, iterator last) {
      while (first != last) first = erase(first);
      return first;
    }

    void clear() { erase(begin(), end()); }
    void fast_clear() { m_root.next = m_root.prev = &m_root; }
    static void remove(value_type* v) { unlink(v); }

  private:
    static INLINE node_type* upcast(intrusive_list_node* n) {
      return static_cast<node_type*>(n);
    }
    static INLINE const node_type* upcast(const intrusive_list_node* n) {
      return static_cast<const node_type*>(n);
    }
  };
} /* namespace gbe */

#endif /* __GBE_INTRUSIVE_LIST_HPP__ */

