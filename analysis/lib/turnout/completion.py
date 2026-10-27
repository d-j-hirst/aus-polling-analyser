"""Use original local count sizes as evidence of nearing completion.

A count approaching its expected size may need few further additions. A count
far above that expectation can instead indicate an undersized prior. These
competing explanations change only the local routine-addition component;
existing exceptional batches and postal receipt safeguards remain available.
"""

import numpy as np
from scipy.special import expit, logit, ndtr, ndtri

SURPRISE_SCALE = 2.
COUNT_EQUIVALENT = .5


def evidence(prepared, inputs, units, progress):
    """Measure each open service against its frozen, unconditioned size draws.

    The original account is carried separately through allocation. Never substitute
    the current conditioned account or an earlier snapshot's posterior. A missing,
    structurally zero or fixed prior supplies no new completion evidence.
    """
    weight = np.zeros(len(units))
    z = np.full(len(units), np.nan)
    frozen = prepared.get('original_unit_counts')
    if frozen is None:
        return weight, z
    for j, unit in enumerate(units):
        if (unit['kind'] != 'declaration' or prepared['complete'][j]
                or unit['counted'] <= 0 or not np.any(frozen[:,j] > 0)):
            continue
        parent = inputs['enrolment'][unit['seat_index']]+2*COUNT_EQUIVALENT
        transformed = logit((frozen[:,j]+COUNT_EQUIVALENT)/parent)
        sd = transformed.std()
        if sd <= 1e-8:
            continue
        z[j] = (logit((unit['counted']+COUNT_EQUIVALENT)/parent)-transformed.mean())/sd
        weight[j] = ndtr(z[j])/(1+(max(z[j],0)/SURPRISE_SCALE)**4)
        if unit.get('vote_type',unit['name']) == 'Postal':
            weight[j] *= progress[j].get('postal_receipt_support',1.)
    return weight, z


def limits(reference, small, batch, capacity, weights, probabilities, positive, uniforms,
           small_sd=1., batch_sd=1.5):
    """Draw local tiny-addition limits from the routine probability interval.

    The ordinary inverse-mixture sample remains unchanged for allocation purposes.
    Only afterwards is the selected category reduced to this limit, so its released
    allowance cannot raise another category. Infinity means no local intervention.
    The routine interval's rescaled position is uniform, giving the tiny component
    its existing continuous log-odds distribution without another random stream.
    """
    previous, small_probability, batch_probability = probabilities
    below = (small_probability*ndtr((reference-small)/small_sd)
             +batch_probability*ndtr((reference-batch)/batch_sd))/positive
    mass = previous/positive*weights
    position = np.divide(uniforms-below,mass,out=np.zeros_like(uniforms),where=mass>0)
    selected = (mass>0)&(position>0)&(position<1)
    result = np.full(uniforms.shape,np.inf)
    # Evaluate only interior positions. Values outside the selected probability
    # interval must not produce invalid inverse-normal or logarithm warnings.
    result[selected] = (capacity*expit(small+small_sd*ndtri(np.where(selected,position,.5))))[selected]
    return result
