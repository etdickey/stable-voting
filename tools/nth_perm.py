"""Return a permutation by zero-based lexicographic rank."""
import math

def nth_permutation(seq, n):
    """
    Return the nth permutation (0-indexed) of the sequence `seq`.
    seq can be a list, string, or any iterable.
    """
    items = list(seq)
    k = len(items)
    fact = math.factorial(k)
    if n < 0 or n >= fact:
        raise ValueError("n must be in range [0, k!)")

    result = []
    for i in range(k, 0, -1):
        fact //= i
        index, n = divmod(n, fact)
        result.append(items.pop(index))
    return result

# Example usage
n = 11
perm = 234074
print(nth_permutation(range(1, n+1), perm))  # perm = 14 means 15th permutation (0-based index)
# print("".join(nth_permutation("abcd", 14)))
