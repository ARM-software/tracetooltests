#pragma once

#include <c/engine.h>
#include <string>
#include <vector>
#include <cstddef>

struct litert_config_t
{
	std::string model_path;
	std::string backend = "gpu";
	int max_output_tokens = 32;
	float temperature = 0.0f;
	float top_p = 0.95f;
	int top_k = 40;
	int seed = 42;
	bool enable_benchmark = true;
};

struct litert_context_t
{
	LiteRtLmEngineSettings* settings = nullptr;
	LiteRtLmEngine* engine = nullptr;
	LiteRtLmSessionConfig* session_config = nullptr;
	LiteRtLmSession* session = nullptr;
	bool active = false;
};

struct litert_benchmark_metrics_t
{
	double time_to_first_token = 0.0;
	double total_init_time_sec = 0.0;
	double prefill_tokens_per_sec = 0.0;
	double decode_tokens_per_sec = 0.0;
	size_t prefill_token_count = 0;
	size_t decode_token_count = 0;
};

std::string litert_find_model(const std::string& user_specified);
bool litert_init(litert_context_t& ctx, const litert_config_t& cfg);
void litert_cleanup(litert_context_t& ctx);
void litert_reset_session(litert_context_t& ctx);
std::string litert_query(litert_context_t& ctx, const std::string& prompt);
bool litert_get_metrics(litert_context_t& ctx, litert_benchmark_metrics_t& metrics);

void litert_show_usage();
bool litert_parse_cmdopt(int& i, int argc, char** argv, litert_config_t& cfg);
