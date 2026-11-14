# ============================================================================
# Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
# Feature: Classical Non-LLM NLP Methodology
# Package: src.NLP.NLPAlgo
# ============================================================================

__all__ = ["ClassicalNLPPipeline"]


def __getattr__(name):
    if name == "ClassicalNLPPipeline":
        from .classical_pipeline import ClassicalNLPPipeline
        return ClassicalNLPPipeline
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
