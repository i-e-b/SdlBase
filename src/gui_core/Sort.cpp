#include "Sort.h"

// minimal sort
inline bool cmp(SwitchPoint* a, int idx1, int idx2) {
    // sort by position, with `on` to the left of `off`
    auto p1 = (a[idx1].xpos << 1u) + a[idx1].state;
    auto p2 = (a[idx2].xpos << 1u) + a[idx2].state;
    return (p1 < p2);
}

// Merge with minimal copies
SwitchPoint* IterativeMergeSort(SwitchPoint* source, SwitchPoint* tmp, int n) {
    if (n < 2) return source;

    auto arr1 = source;
    auto arr2 = tmp;

    auto A = arr2; // we will be flipping the array pointers around
    auto B = arr1;

    for (unsigned int stride = 1; stride < n; stride <<= 1u) { // doubling merge width
        
        // swap A and B pointers after each merge set
        { auto swp = A; A = B; B = swp; }

        int t = 0; // incrementing point in target array
        for (uint32_t left = 0; left < n; left += stride << 1u) {
            uint32_t right = left + stride;
            uint32_t end = right + stride;
            if (end > n) end = n; // some merge windows will run off the end of the data array
            if (right > n) right = n; // some merge windows will run off the end of the data array
            int l = left, r = right; // the point we are scanning though the two sets to be merged.

            // copy the lowest candidate across from A to B
            while (l < right && r < end) {
                if (cmp(A, l, r)) { // compare the two bits to be merged
                    B[t++] = A[l++];
                } else {
                    B[t++] = A[r++];
                }
            } // exhausted at least one of the merge sides

            while (l < right) { // run down left if anything remains
                B[t++] = A[l++];
            }

            while (r < end) { // run down right side if anything remains
                B[t++] = A[r++];
            }
        }
    }

    return B; // return the actual result, whatever that is.
}