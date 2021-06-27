# -*- coding: utf-8 -*-
"""
Created on Sun Sep 13 00:51:38 2020

@author: danny
"""

import sys
import re
import gzip
import pystan
import pickle
from hashlib import md5

def stan_cache(model_code, m_name=None):
    code_hash = md5(model_code.encode('ascii')).hexdigest()
    
    pre = './stan-cache/' # Cache home dir
    fname = pre + m_name if m_name else pre
    fname += ('-' + sys.version + '-' +pystan.__version__ + '-' + code_hash+ '.pkl.gz')
    rex = re.compile('[^a-zA-Z0-9_ ,/\.\-]')
    fname = rex.sub('', fname)
    try:
        sm = pickle.load(gzip.open(fname, 'rb'))
    except:
        print("About to compile model")
        sm = pystan.StanModel(model_code=model_code, verbose=True)
        with gzip.open(fname, 'wb') as fh:
            pickle.dump(sm, fh)
    else:
        print("Using cached model")
            
    return sm