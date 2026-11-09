# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# Package: src.NLP.NLPAlgo.knowledge_injection
# ============================================================================

from .wordnet_synsets import WordNetEnricher
from .antonym_inverter import AntonymPolarityInverter

__all__ = ["WordNetEnricher", "AntonymPolarityInverter"]
