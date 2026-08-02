"""Transform vote shares between percentage and model-friendly log-odds scales.

This is a mathematical support module: it loads no files and writes no
outputs. Its functions define the common transformation used by historical
analysis, trend adjustment and regional modelling.

Main functions:
* ``clamp`` constrains a numeric value to inclusive bounds.
* ``transform_vote_share`` converts a percentage to the scaled logit scale.
* ``detransform_vote_share`` converts the scaled logit value to a percentage.
"""

import math

def clamp(n, min_n, max_n):
    return max(min(max_n, n), min_n)


def transform_vote_share(vote_share):
    vote_share = clamp(vote_share, 0.1, 99.9)
    return math.log((vote_share * 0.01) / (1 - vote_share * 0.01)) * 25


def detransform_vote_share(vote_share):
    return 100 / (1 + math.exp(-0.04 * vote_share))

