# ============================================================================
# Sodium - Automated Reasoning & Neuro-Symbolic Verification Engine
# Feature: Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/classical_pipeline.py
# Description: Top-level orchestrator executing the 7-step classical NLP pipeline:
#              1. Tokenization & Normalization (Math Shielding + Lemmatization)
#              2. Syntactic & Linguistic Parsing (POS Tagging + Dependencies)
#              3. Rule-Based Entity Extraction (Regex + Levenshtein / Jaro-Winkler)
#              4. Structural Knowledge Injection (WordNet Synsets + Antonyms)
#              5. Lexicon-Based Feature Scoring (VADER Polarity + TextRank)
#              6. Static Vectorization (Zero-GPU Word2Vec / GloVe lookup)
#              7. Vector Aggregation & Scoring (TF-IDF + Graph Centrality + CNF Synthesis)
# ============================================================================

from __future__ import annotations
import sys
import json
import argparse
from pathlib import Path
from typing import Dict, Any, List, Optional, Tuple

# Ensure parent NLP directory is in sys.path
_current_dir = Path(__file__).resolve().parent
_nlp_dir = _current_dir.parent
if str(_nlp_dir) not in sys.path:
    sys.path.insert(0, str(_nlp_dir))

# Step 1: Tokenization & Normalization
try:
    from .tokenization_normalization.math_tokenizer import MathTokenizer
    from .tokenization_normalization.lemmatizer import LogicLemmatizer
    from .syntactic_parsing.pos_tagger import RulePOSTagger, ModalType
    from .syntactic_parsing.dependency_parser import LogicalDependencyParser
    from .entity_extraction.pattern_extractor import EntityPatternExtractor
    from .entity_extraction.distance_matcher import EntityDistanceMatcher
    from .knowledge_injection.wordnet_synsets import WordNetEnricher
    from .knowledge_injection.antonym_inverter import AntonymPolarityInverter
    from .lexicon_scoring.polarity_scorer import RequirementPolarityScorer
    from .lexicon_scoring.textrank_extractor import TextRankKeyphraseExtractor
    from .static_vectorization.static_embeddings import StaticEmbeddingMatrix
    from .static_vectorization.vector_utils import VectorUtils
    from .vector_aggregation.tfidf_graph_centrality import TFIDFGraphCentrality
    from .vector_aggregation.cnf_synthesizer import CNFSynthesizer
    from ..llm_router import SodiumCNFRequest, SodiumExecutionBridge
except (ImportError, ValueError):
    from NLPAlgo.tokenization_normalization.math_tokenizer import MathTokenizer
    from NLPAlgo.tokenization_normalization.lemmatizer import LogicLemmatizer
    from NLPAlgo.syntactic_parsing.pos_tagger import RulePOSTagger, ModalType
    from NLPAlgo.syntactic_parsing.dependency_parser import LogicalDependencyParser
    from NLPAlgo.entity_extraction.pattern_extractor import EntityPatternExtractor
    from NLPAlgo.entity_extraction.distance_matcher import EntityDistanceMatcher
    from NLPAlgo.knowledge_injection.wordnet_synsets import WordNetEnricher
    from NLPAlgo.knowledge_injection.antonym_inverter import AntonymPolarityInverter
    from NLPAlgo.lexicon_scoring.polarity_scorer import RequirementPolarityScorer
    from NLPAlgo.lexicon_scoring.textrank_extractor import TextRankKeyphraseExtractor
    from NLPAlgo.static_vectorization.static_embeddings import StaticEmbeddingMatrix
    from NLPAlgo.static_vectorization.vector_utils import VectorUtils
    from NLPAlgo.vector_aggregation.tfidf_graph_centrality import TFIDFGraphCentrality
    from NLPAlgo.vector_aggregation.cnf_synthesizer import CNFSynthesizer
    from llm_router import SodiumCNFRequest, SodiumExecutionBridge


class ClassicalNLPPipeline:
    """
    Offline, deterministic NLP reasoning bridge compiling natural language
    specifications and mathematical equations into Conjunctive Normal Form.
    """

    def __init__(self, similarity_threshold: float = 0.82):
        self.tokenizer = MathTokenizer()
        self.lemmatizer = LogicLemmatizer()
        self.pos_tagger = RulePOSTagger()
        self.dep_parser = LogicalDependencyParser()
        self.distance_matcher = EntityDistanceMatcher(similarity_threshold=similarity_threshold)
        self.knowledge_enricher = WordNetEnricher()
        self.antonym_inverter = AntonymPolarityInverter()
        self.polarity_scorer = RequirementPolarityScorer()
        self.textrank = TextRankKeyphraseExtractor()
        self.embedding_matrix = StaticEmbeddingMatrix()
        self.tfidf_centrality = TFIDFGraphCentrality()
        self.synthesizer = CNFSynthesizer(self.distance_matcher)
        self.bridge = SodiumExecutionBridge()

    def compile_text(self, raw_text: str) -> SodiumCNFRequest:
        """
        Executes the full 7-step pipeline on raw text and returns a validated SodiumCNFRequest.
        """
        if not raw_text or not raw_text.strip():
            raise ValueError("Input text for Classical NLP pipeline cannot be empty.")

        # --------------------------------------------------------------------
        # Step 1: Tokenization, Normalization & Math Equation Preservation
        # --------------------------------------------------------------------
        sentences, math_map = self.tokenizer.split_rules(raw_text)
        if not sentences:
            sentences = [raw_text.strip()]

        normalized_sentences: List[str] = []
        tokenized_rules: List[List[str]] = []
        for s in sentences:
            norm_s = self.lemmatizer.normalize_sentence(s)
            tokens = self.tokenizer.tokenize_words(norm_s)
            normalized_sentences.append(norm_s)
            tokenized_rules.append(tokens)

        # --------------------------------------------------------------------
        # Step 2: Syntactic & Linguistic Parsing
        # --------------------------------------------------------------------
        parsed_rules = []
        for idx, s in enumerate(sentences):
            parsed = self.dep_parser.parse_rule(idx + 1, s)
            parsed.normalized_text = normalized_sentences[idx]
            parsed_rules.append(parsed)

        # --------------------------------------------------------------------
        # Step 3 & 4: Entity Extraction & Knowledge Injection
        # --------------------------------------------------------------------
        # Register and canonicalize entities across parsed clauses
        for parsed in parsed_rules:
            if parsed.antecedent:
                self.synthesizer._resolve_literal(parsed.antecedent, math_map)
            if parsed.consequent:
                self.synthesizer._resolve_literal(parsed.consequent, math_map)

        # --------------------------------------------------------------------
        # Step 5: Lexicon-Based Feature Scoring & Keyphrase Centrality
        # --------------------------------------------------------------------
        for idx, parsed in enumerate(parsed_rules):
            scores = self.polarity_scorer.score_rule(parsed.raw_text)
            keyphrases = self.textrank.extract_keyphrases(tokenized_rules[idx], top_n=3)
            # Salience weighting
            parsed.salience_score = 1.0 + abs(scores["compound"])

        # --------------------------------------------------------------------
        # Step 6 & 7: Static Vectorization, Aggregation & Logic Synthesis
        # --------------------------------------------------------------------
        rule_vectors = self.tfidf_centrality.compute_tfidf_vectors(tokenized_rules)
        centrality_scores = self.tfidf_centrality.compute_rule_centrality(rule_vectors)

        # Attach centrality to rules
        for idx, parsed in enumerate(parsed_rules):
            if idx < len(centrality_scores):
                parsed.salience_score *= (1.0 + centrality_scores[idx])

        # Synthesize CNF clauses
        clauses, assumptions = self.synthesizer.synthesize_clauses(parsed_rules, math_map)

        # Construct final Pydantic request dictionary
        payload_dict = self.synthesizer.build_pydantic_payload(
            clauses=clauses,
            assumptions=assumptions,
            description="Compiled via Classical Non-LLM NLP Pipeline (Rule & Math Compiler)"
        )

        json_str = json.dumps(payload_dict)
        return SodiumCNFRequest.from_json_string(json_str)

    def solve(self, cnf_request: SodiumCNFRequest) -> Dict[str, Any]:
        """Invokes Sodium's C CDCL engine via ctypes if shared library is built."""
        return self.bridge.solve_json(cnf_request.to_json_string())


def main():
    parser = argparse.ArgumentParser(
        description="Sodium Classical Non-LLM NLP Compiler: Translates natural language & math to CDCL CNF."
    )
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument("-t", "--text", type=str, help="Input natural language specification.")
    input_group.add_argument("-f", "--file", type=str, help="Path to specification text file.")

    parser.add_argument("-o", "--output", type=str, default=None, help="Output JSON destination file.")
    parser.add_argument("-s", "--solve", action="store_true", help="Solve directly with Sodium C CDCL engine.")

    args = parser.parse_args()

    input_text = ""
    if args.text:
        input_text = args.text
    elif args.file:
        input_text = Path(args.file).read_text(encoding="utf-8")

    pipeline = ClassicalNLPPipeline()
    try:
        cnf_request = pipeline.compile_text(input_text)
    except Exception as e:
        sys.stderr.write(f"Error during Classical NLP compilation: {e}\n")
        sys.exit(1)

    json_str = cnf_request.to_json_string(indent=2)
    if args.output:
        Path(args.output).write_text(json_str, encoding="utf-8")
        print(f"Validated CNF JSON written to {args.output}")
    else:
        print(json_str)

    if args.solve:
        print("\n--- Invoking Sodium CDCL C Engine ---")
        if not pipeline.bridge.is_available():
            sys.stderr.write("Warning: libsodium_sat shared library not found. Build with 'make' first.\n")
            sys.exit(2)
        res = pipeline.solve(cnf_request)
        print(json.dumps(res, indent=2))


if __name__ == "__main__":
    main()
