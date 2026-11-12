#include "litert_common.h"
#include "util.h"

#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <ctime>

std::string litert_find_model(const std::string& user_specified)
{
	if (!user_specified.empty())
	{
		if (access(user_specified.c_str(), R_OK) == 0)
		{
			return user_specified;
		}
		return "";
	}

	const char* env_path = getenv("LITERT_MODEL_PATH");
	if (env_path && *env_path && access(env_path, R_OK) == 0)
	{
		return std::string(env_path);
	}

	const char* home = getenv("HOME");
	if (home && *home)
	{
		const std::string candidates[] = {
			std::string(home) + "/.litert-lm/models/gemma3-1b-it-int4.litertlm",
			std::string(home) + "/.litert-lm/models/gemma-4-E2B-it.litertlm",
			std::string(home) + "/.litert-lm/models/Qwen3-0.6B.litertlm",
			std::string(home) + "/.litert-lm/models/gemma3-1b-gpu-custom/model.litertlm",
			std::string(home) + "/LiteRT-LM/runtime/testdata/test_lm.litertlm"
		};
		for (const auto& c : candidates)
		{
			if (access(c.c_str(), R_OK) == 0)
			{
				return c;
			}
		}
	}

	const std::string system_candidates[] = {
		"/opt/litert-lm/models/model.litertlm",
		"./model.litertlm"
	};
	for (const auto& c : system_candidates)
	{
		if (access(c.c_str(), R_OK) == 0)
		{
			return c;
		}
	}

	return "";
}

bool litert_init(litert_context_t& ctx, const litert_config_t& cfg)
{
	if (cfg.model_path.empty())
	{
		fprintf(stderr, "No LiteRT-LM model path provided.\n");
		return false;
	}

	printf("Initializing LiteRT-LM with model: %s (backend: %s)\n", cfg.model_path.c_str(), cfg.backend.c_str());
	LiteRtLmStatusCode status = litert_lm_engine_settings_create(
		cfg.model_path.c_str(), cfg.backend.c_str(), nullptr, nullptr, &ctx.settings);
	if (status != kLiteRtLmStatusOk || !ctx.settings)
	{
		fprintf(stderr, "litert_lm_engine_settings_create failed: %d\n", status);
		return false;
	}

	if (cfg.enable_benchmark)
	{
		litert_lm_engine_settings_enable_benchmark(ctx.settings);
	}

	status = litert_lm_engine_create(ctx.settings, &ctx.engine);
	if (status != kLiteRtLmStatusOk || !ctx.engine)
	{
		fprintf(stderr, "litert_lm_engine_create failed: %d\n", status);
		litert_lm_engine_settings_delete(ctx.settings);
		ctx.settings = nullptr;
		return false;
	}

	status = litert_lm_session_config_create(&ctx.session_config);
	if (status != kLiteRtLmStatusOk || !ctx.session_config)
	{
		fprintf(stderr, "litert_lm_session_config_create failed: %d\n", status);
		litert_lm_engine_delete(ctx.engine);
		litert_lm_engine_settings_delete(ctx.settings);
		ctx.engine = nullptr;
		ctx.settings = nullptr;
		return false;
	}

	litert_lm_session_config_set_max_output_tokens(ctx.session_config, cfg.max_output_tokens);

	LiteRtLmSamplerParams* sampler_params = nullptr;
	if (litert_lm_sampler_params_create(kLiteRtLmSamplerTypeTopP, &sampler_params) == kLiteRtLmStatusOk && sampler_params)
	{
		litert_lm_sampler_params_set_top_p(sampler_params, cfg.top_p);
		litert_lm_sampler_params_set_top_k(sampler_params, cfg.top_k);
		litert_lm_sampler_params_set_temperature(sampler_params, cfg.temperature);
		int32_t chosen_seed = (cfg.seed >= 0) ? cfg.seed : static_cast<int32_t>(time(nullptr) ^ (getpid() << 16));
		litert_lm_sampler_params_set_seed(sampler_params, chosen_seed);
		litert_lm_session_config_set_sampler_params(ctx.session_config, sampler_params);
		litert_lm_sampler_params_delete(sampler_params);
	}

	status = litert_lm_engine_create_session(ctx.engine, ctx.session_config, &ctx.session);
	if (status != kLiteRtLmStatusOk || !ctx.session)
	{
		fprintf(stderr, "litert_lm_engine_create_session failed: %d\n", status);
		litert_lm_session_config_delete(ctx.session_config);
		litert_lm_engine_delete(ctx.engine);
		litert_lm_engine_settings_delete(ctx.settings);
		ctx.session_config = nullptr;
		ctx.engine = nullptr;
		ctx.settings = nullptr;
		return false;
	}

	ctx.active = true;
	printf("LiteRT-LM engine and session successfully initialized.\n");
	return true;
}

void litert_cleanup(litert_context_t& ctx)
{
	if (ctx.session)
	{
		litert_lm_session_delete(ctx.session);
		ctx.session = nullptr;
	}
	if (ctx.session_config)
	{
		litert_lm_session_config_delete(ctx.session_config);
		ctx.session_config = nullptr;
	}
	if (ctx.engine)
	{
		litert_lm_engine_delete(ctx.engine);
		ctx.engine = nullptr;
	}
	if (ctx.settings)
	{
		litert_lm_engine_settings_delete(ctx.settings);
		ctx.settings = nullptr;
	}
	ctx.active = false;
}

void litert_reset_session(litert_context_t& ctx)
{
	if (ctx.session)
	{
		litert_lm_session_delete(ctx.session);
		ctx.session = nullptr;
	}
	if (ctx.engine && ctx.session_config)
	{
		litert_lm_engine_create_session(ctx.engine, ctx.session_config, &ctx.session);
	}
}

std::string litert_query(litert_context_t& ctx, const std::string& prompt)
{
	if (!ctx.active || !ctx.session)
	{
		return "";
	}

	LiteRtLmInputData* input_data = nullptr;
	LiteRtLmStatusCode status = litert_lm_input_data_create(
		kLiteRtLmInputDataTypeText, prompt.c_str(), prompt.size(), &input_data);
	if (status != kLiteRtLmStatusOk || !input_data)
	{
		return "";
	}

	const LiteRtLmInputData* inputs[] = { input_data };
	LiteRtLmResponses* responses = nullptr;
	status = litert_lm_session_generate_content(ctx.session, inputs, 1, &responses);

	if (status == kLiteRtLmStatusResourceExhausted)
	{
		litert_reset_session(ctx);
		if (ctx.session)
		{
			status = litert_lm_session_generate_content(ctx.session, inputs, 1, &responses);
		}
	}

	std::string result_text;
	if (status == kLiteRtLmStatusOk && responses)
	{
		size_t count = 0;
		litert_lm_responses_get_num_candidates(responses, &count);
		if (count > 0)
		{
			const char* text = nullptr;
			if (litert_lm_responses_get_response_text_at(responses, 0, &text) == kLiteRtLmStatusOk && text)
			{
				result_text = text;
			}
		}
		litert_lm_responses_delete(responses);
	}
	litert_lm_input_data_delete(input_data);
	return result_text;
}

bool litert_get_metrics(litert_context_t& ctx, litert_benchmark_metrics_t& metrics)
{
	if (!ctx.active || !ctx.session)
	{
		return false;
	}

	LiteRtLmBenchmarkInfo* binfo = nullptr;
	LiteRtLmStatusCode status = litert_lm_session_get_benchmark_info(ctx.session, &binfo);
	if (status != kLiteRtLmStatusOk || !binfo)
	{
		return false;
	}

	litert_lm_benchmark_info_get_time_to_first_token(binfo, &metrics.time_to_first_token);
	litert_lm_benchmark_info_get_total_init_time_in_second(binfo, &metrics.total_init_time_sec);

	size_t prefill_turns = 0;
	if (litert_lm_benchmark_info_get_num_prefill_turns(binfo, &prefill_turns) == kLiteRtLmStatusOk && prefill_turns > 0)
	{
		litert_lm_benchmark_info_get_prefill_token_count_at(binfo, 0, &metrics.prefill_token_count);
		litert_lm_benchmark_info_get_prefill_tokens_per_sec_at(binfo, 0, &metrics.prefill_tokens_per_sec);
	}

	size_t decode_turns = 0;
	if (litert_lm_benchmark_info_get_num_decode_turns(binfo, &decode_turns) == kLiteRtLmStatusOk && decode_turns > 0)
	{
		litert_lm_benchmark_info_get_decode_token_count_at(binfo, 0, &metrics.decode_token_count);
		litert_lm_benchmark_info_get_decode_tokens_per_sec_at(binfo, 0, &metrics.decode_tokens_per_sec);
	}

	litert_lm_benchmark_info_delete(binfo);
	return true;
}

void litert_show_usage()
{
	printf("-m/--model <path>         Path to .litertlm model file (auto-detected if omitted)\n");
	printf("-b/--backend <cpu|gpu>    LiteRT-LM backend (default 'gpu')\n");
	printf("-n/--max-tokens <N>       Maximum output tokens to generate (default 32)\n");
	printf("-t/--temperature <val>    Sampling temperature (default 0.0)\n");
	printf("-s/--seed <N>             Random seed for sampling (default 42)\n");
	printf("--benchmark               Enable benchmark info reporting\n");
}

bool litert_parse_cmdopt(int& i, int argc, char** argv, litert_config_t& cfg)
{
	if (match(argv[i], "-m", "--model"))
	{
		cfg.model_path = get_string_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-b", "--backend"))
	{
		cfg.backend = get_string_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-n", "--max-tokens"))
	{
		cfg.max_output_tokens = get_arg(argv, ++i, argc);
		assert(cfg.max_output_tokens > 0);
		return true;
	}
	if (match(argv[i], "-t", "--temperature"))
	{
		cfg.temperature = static_cast<float>(atof(get_string_arg(argv, ++i, argc)));
		return true;
	}
	if (match(argv[i], "-s", "--seed"))
	{
		cfg.seed = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], nullptr, "--benchmark"))
	{
		cfg.enable_benchmark = true;
		return true;
	}
	return false;
}
