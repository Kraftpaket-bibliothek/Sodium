# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/knowledge_injection/wordnet_synsets.py
# Description: WordNet synset and hypernym knowledge injection for logic concepts.
# ============================================================================

from __future__ import annotations
from typing import Set, List, Dict

try:
    from nltk.corpus import wordnet as wn
    _WN_AVAILABLE = True
except Exception:
    _WN_AVAILABLE = False


class WordNetEnricher:
    """
    Injects ontological and lexical knowledge from WordNet hierarchies,
    enriching logical entities with synonyms and hypernym abstractions.
    """

    # Domain-specific synonym clusters for verification & logic
    DOMAIN_SYNONYMS: Dict[str, Set[str]] = {
        "active": {"online", "enabled", "running", "operational", "live", "up", "working"},
        "inactive": {"offline", "disabled", "stopped", "down", "halted", "dormant"},
        "start": {"activate", "enable", "boot", "launch", "initiate", "trigger"},
        "stop": {"deactivate", "disable", "halt", "terminate", "kill", "shut down"},
        "require": {"demand", "mandate", "necessitate", "need", "enforce"},
        "permit": {"allow", "authorize", "grant", "enable"},
        "forbid": {"prohibit", "disallow", "deny", "block", "ban", "prevent"},
        "true": {"valid", "verified", "satisfied", "positive", "asserted"},
        "false": {"invalid", "contradictory", "unsatisfied", "negative", "refuted"},
        "server": {"node", "host", "machine", "instance", "replica"},
        "database": {"db", "datastore", "repository", "storage"},
    }

    def get_synonyms(self, word: str) -> Set[str]:
        """
        Retrieves synonyms for a given word using WordNet and domain knowledge.
        """
        low = word.lower()
        synonyms: Set[str] = set()

        # Domain table lookup
        for canonical, syns in self.DOMAIN_SYNONYMS.items():
            if low == canonical or low in syns:
                synonyms.update(syns)
                synonyms.add(canonical)

        # WordNet lookup if available
        if _WN_AVAILABLE:
            try:
                for syn in wn.synsets(low):
                    for lemma in syn.lemmas():
                        name = lemma.name().replace('_', ' ').lower()
                        synonyms.add(name)
            except Exception:
                pass

        return synonyms

    def get_hypernyms(self, word: str) -> Set[str]:
        """
        Retrieves hypernyms (broader category concepts) for a word.
        """
        low = word.lower()
        hypernyms: Set[str] = set()

        if _WN_AVAILABLE:
            try:
                for syn in wn.synsets(low):
                    for hyper in syn.hypernyms():
                        for lemma in hyper.lemmas():
                            hypernyms.add(lemma.name().replace('_', ' ').lower())
            except Exception:
                pass

        return hypernyms
