// Minimal LiteRT-LM microbenchmark test.

#include "litert_common.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>

static void show_test_usage()
{
	printf("Usage: litert_minimal [options]\n");
	printf("-h/--help              Show this help\n");
	printf("-p/--prompt <text>     Input prompt for text generation\n");
	litert_show_usage();
}

int main(int argc, char** argv)
{
	litert_config_t cfg;
	cfg.model_path = "";
	cfg.backend = "gpu";
	cfg.max_output_tokens = 24;
	cfg.temperature = 0.0f; // Deterministic / greedy
	cfg.seed = 42;
	cfg.enable_benchmark = true;

	std::string prompt = "The quick brown fox jumps over the lazy dog.";

	for (int i = 1; i < argc; ++i)
	{
		if (match(argv[i], "-h", "--help"))
		{
			show_test_usage();
			return 0;
		}
		if (match(argv[i], "-p", "--prompt"))
		{
			prompt = get_string_arg(argv, ++i, argc);
			continue;
		}
		if (litert_parse_cmdopt(i, argc, argv, cfg))
		{
			continue;
		}
		fprintf(stderr, "Unknown parameter: %s\n", argv[i]);
		show_test_usage();
		return 1;
	}

	cfg.model_path = litert_find_model(cfg.model_path);
	if (cfg.model_path.empty())
	{
		printf("No LiteRT-LM model found (specify via --model or LITERT_MODEL_PATH). Skipping test.\n");
		return 77;
	}

	litert_context_t ctx;
	if (!litert_init(ctx, cfg))
	{
		fprintf(stderr, "Failed to initialize LiteRT-LM engine.\n");
		return 77;
	}

	printf("Running inference with prompt: \"%s\"\n", prompt.c_str());
	std::string response = litert_query(ctx, prompt);

	const char* null_run = getenv("TOOLSTEST_NULL_RUN");
	bool is_null_run = (null_run && strcmp(null_run, "1") == 0);

	if (!is_null_run)
	{
		assert(!response.empty());
	}

	printf("LLM Response: %s\n", response.c_str());

	litert_benchmark_metrics_t metrics;
	if (litert_get_metrics(ctx, metrics))
	{
		printf("--- Benchmark Metrics ---\n");
		printf("Time to first token:     %.4f s\n", metrics.time_to_first_token);
		printf("Engine init time:        %.4f s\n", metrics.total_init_time_sec);
		printf("Prefill token count:     %zu\n", metrics.prefill_token_count);
		printf("Prefill tokens/sec:      %.2f\n", metrics.prefill_tokens_per_sec);
		printf("Decode token count:      %zu\n", metrics.decode_token_count);
		printf("Decode tokens/sec:       %.2f\n", metrics.decode_tokens_per_sec);
		printf("-------------------------\n");
	}

	litert_cleanup(ctx);
	printf("litert_minimal completed successfully!\n");
	return 0;
}
