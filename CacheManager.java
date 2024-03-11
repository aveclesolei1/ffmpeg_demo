package com.study.demo.cache;

import java.util.Deque;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;

//todo 传入的对象必须实现字符串搜索的方法
public class CacheManager<T> {

    public Deque<CachedData<T>> cache;

    public CacheManager(LinkedList<T> data) {
        cache = new LinkedList<>();
        cache.addFirst(new CachedData<>("", data));
    }

    public LinkedList<T> searchString(String str) {

        while (cache.size() >= 1) {
            final String s = cache.peekFirst().key;
            int diff = s.length() - str.length();
            if (diff < 0) {
                if (s.equals(str.substring(0, str.length() + diff))) {
                    for (int i=-1;i>=diff;i--) {
                        search(str.substring(0, str.length() + diff - i));
                    }
                    return str.equals(cache.peekFirst().key) ? cache.peekFirst().value : null;
                } else {
                    backtrace();
                }
            } else if (diff > 0) {
                backtrace();
            } else {
                if (s.equals(str)) {
                    return cache.peekFirst().value;
                } else {
                    backtrace();
                }
            }
        }

        return null;
    }

    private LinkedList<T> search(String str) {
        //用于返回结果的list
        LinkedList<T> result = new LinkedList<>();

        //遍历过程中删除元素需要用到迭代器，否则会报同步异常
        Iterator<T> iterator = cache.peek().value.iterator();
        while (iterator.hasNext()) {
            T v = iterator.next();
            if (v instanceof String && ((String)v).contains(str)) {
                iterator.remove();
                result.add(v);
            }
        }

        //todo 此处需要考虑一下无搜索结果时是否需要返回null，返回一个空的链表和null外部处理方式不同
        if (result.size() == 0) {
            return null;
        }
        //新的搜索结果需要放入cache中管理
        cache.addFirst(new CachedData<>(str, result));
        return result;
    }

    private void backtrace() {
        if (cache.size() == 1) {
            return;
        }

        LinkedList<T> list = cache.pollFirst().value;
        cache.peekFirst().value.addAll(list);
    }

    private static class CachedData<T> {
        private String key;
        private LinkedList<T> value;

        private CachedData(String key, LinkedList<T> value) {
            this.key = key;
            this.value = value;
        }
    }

    public interface CacheManagerImpl {
        public boolean existString();
    }
}
