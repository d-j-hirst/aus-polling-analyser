import math

def clamp(n, min_n, max_n):
    return max(min(max_n, n), min_n)


def transform_vote_share(vote_share):
    vote_share = clamp(vote_share, 0.1, 99.9)
    return math.log((vote_share * 0.01) / (1 - vote_share * 0.01)) * 25


def detransform_vote_share(vote_share):
    return 100 / (1 + math.exp(-0.04 * vote_share))

