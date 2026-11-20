# Classical NLP to CNF

Sodium's natural-language entry point is a deterministic, local pipeline. It accepts controlled logical statements supported by the parser; it does not provide unrestricted language understanding. Unsupported constructions return a stage-specific error.

## Compilation flow

`ClassicalNLPPipeline.compile_text()` performs these steps:

1. Shields mathematical expressions and splits the input into rules.
2. Normalizes contractions and tokenizes logical operators and propositions.
3. Parses the supported grammar into a logical expression tree.
4. Resolves proposition names to stable, deterministic variable IDs.
5. Converts the expression trees to CNF, including auxiliary variables where required.
6. Validates the result against `SodiumCNFRequest`, the shared JSON contract.

`ClassicalNLPPipeline.solve()` passes only a validated request to the existing C CDCL solver through `SodiumExecutionBridge`.

## Run it

Compile natural-language rules to JSON:

```sh
python3 -m src.NLP.NLPAlgo.classical_pipeline \
  --text "If the engine is running then the battery is connected." \
  --output request.json
```

To compile and solve using the C solver bridge, add `--solve`. The main application also exposes this pipeline through `./sodium --nlp "<rules>"` and its web interface.

The pipeline requires Python and Pydantic for the CNF contract. Install the declared Python dependency from the repository root with `python3 -m pip install -r requirements.txt`. Its tokenizer, parser, resolution, and compiler use local deterministic code. `RulePOSTagger` has an optional NLTK implementation with an internal rule-based fallback, but the controlled-language compiler's parser does not require NLTK.
