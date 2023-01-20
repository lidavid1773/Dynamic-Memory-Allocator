# Dynamic-Memory-Allocator

A dynamic memory allocator for the x86-64 architecture with the following features:

-Free lists segregated by size class, using first-fit policy within each size class.\
-Immediate coalescing of large blocks on free with adjacent free blocks.\
-Boundary tags to support efficient coalescing.\
-Block splitting without creating splinters.\
-Allocated blocks aligned to "quadruple memory row" (32-byte) boundaries.\
-Free lists maintained using last in first out (LIFO) discipline.\
-Use of a prologue and epilogue to achieve required alignment and avoid edge cases at the end of the heap.\
-"Wilderness preservation" heuristic, to avoid unnecessary growing of the heap.\

I have implemented my own versions of the malloc, realloc, and free functions for C.
