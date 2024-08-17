#include <obs-module.h>
#include <util/deque.h>
#include <util/dstr.h>
#include "recursion-effect.h"
#include "version.h"

struct frame {
	gs_texrender_t *render;
	uint64_t ts;
};

struct recursion_effect_info {
	obs_source_t *source;
	obs_hotkey_pair_id hotkey;
	struct deque frames;
	gs_texrender_t *render;
	size_t frame_pos;
	uint64_t delay_ns;
	struct vec2 offset;
	struct vec2 scale;
	float rotation;
	gs_effect_t *effect;
	gs_eparam_t *param_image;
	gs_eparam_t *param_multiplier;
	float alpha;

	uint64_t interval_ns;
	uint32_t cx;
	uint32_t cy;
	bool target_valid;
	bool processed_frame;
	bool inversed;
	long long reset_trigger;
};

static const char *recursion_effect_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("RecursionEffect");
}

static void free_textures(struct recursion_effect_info *f)
{
	if (!f->frames.size && !f->render)
		return;
	obs_enter_graphics();
	while (f->frames.size) {
		struct frame frame;
		deque_pop_front(&f->frames, &frame, sizeof(frame));
		gs_texrender_destroy(frame.render);
	}
	deque_free(&f->frames);
	if (f->render) {
		gs_texrender_destroy(f->render);
		f->render = NULL;
	}
	obs_leave_graphics();
}

static size_t num_frames(struct deque *buf)
{
	return buf->size / sizeof(struct frame);
}

static void update_interval(struct recursion_effect_info *f,
			    uint64_t new_interval_ns)
{
	if (!f->target_valid || !new_interval_ns) {
		free_textures(f);
		return;
	}

	if (!f->render) {
		obs_enter_graphics();
		f->render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		obs_leave_graphics();
	}
	f->interval_ns = new_interval_ns;
	size_t num = (size_t)(f->delay_ns / new_interval_ns);
	if (!num)
		num = 1;

	if (num > num_frames(&f->frames)) {
		size_t prev_num = num_frames(&f->frames);

		obs_enter_graphics();

		deque_upsize(&f->frames, num * sizeof(struct frame));

		for (size_t i = prev_num; i < num; i++) {
			struct frame *frame =
				deque_data(&f->frames, i * sizeof(*frame));
			frame->render =
				gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		}

		obs_leave_graphics();

	} else if (num < num_frames(&f->frames)) {
		obs_enter_graphics();

		while (num_frames(&f->frames) > num) {
			struct frame frame;
			deque_pop_front(&f->frames, &frame, sizeof(frame));
			gs_texrender_destroy(frame.render);
		}

		obs_leave_graphics();
	}
}

static inline void check_interval(struct recursion_effect_info *f)
{
	struct obs_video_info ovi = {0};
	uint64_t interval_ns;

	obs_get_video_info(&ovi);

	interval_ns = util_mul_div64(ovi.fps_den, 1000000000ULL, ovi.fps_num);

	if (interval_ns != f->interval_ns)
		update_interval(f, interval_ns);
}

static inline void reset_textures(struct recursion_effect_info *f)
{
	f->interval_ns = 0;
	free_textures(f);
	check_interval(f);
}

bool recursion_effect_enable_hotkey(void *data, obs_hotkey_pair_id id,
				    obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	struct recursion_effect_info *recursion_effect = data;
	if (!pressed)
		return false;

	if (obs_source_enabled(recursion_effect->source))
		return false;

	obs_source_set_enabled(recursion_effect->source, true);

	return true;
}

bool recursion_effect_disable_hotkey(void *data, obs_hotkey_pair_id id,
				     obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	struct recursion_effect_info *recursion_effect = data;
	if (!pressed)
		return false;
	if (!obs_source_enabled(recursion_effect->source))
		return false;

	obs_source_set_enabled(recursion_effect->source, false);
	return true;
}

static void recursion_effect_update(void *data, obs_data_t *settings)
{
	struct recursion_effect_info *recursion_effect = data;

	if (recursion_effect->hotkey == OBS_INVALID_HOTKEY_PAIR_ID) {
		obs_source_t *parent =
			obs_filter_get_parent(recursion_effect->source);
		if (parent) {
			recursion_effect
				->hotkey = obs_hotkey_pair_register_source(
				parent, "RecursionEffect.Enable",
				obs_module_text("RecursionEffectEnable"),
				"RecursionEffect.Disable",
				obs_module_text("RecursionEffectDisable"),
				recursion_effect_enable_hotkey,
				recursion_effect_disable_hotkey,
				recursion_effect, recursion_effect);
		}
	}
	const long long d = obs_data_get_int(settings, S_DELAY_MS);
	const uint64_t delay_ns = (d > 0 ? (uint64_t)d : 1) * 1000000ULL;
	if (delay_ns != recursion_effect->delay_ns) {
		recursion_effect->delay_ns = delay_ns;
		if (recursion_effect->interval_ns)
			update_interval(recursion_effect,
					recursion_effect->interval_ns);
	}

	recursion_effect->offset.x =
		(float)obs_data_get_double(settings, S_OFFSET_X);
	recursion_effect->offset.y =
		(float)obs_data_get_double(settings, S_OFFSET_Y);
	recursion_effect->scale.x =
		(float)obs_data_get_double(settings, S_SCALE_X);
	recursion_effect->scale.y =
		(float)obs_data_get_double(settings, S_SCALE_Y);
	recursion_effect->rotation =
		(float)obs_data_get_double(settings, S_ROTATION);
	recursion_effect->alpha =
		(float)obs_data_get_double(settings, S_ALPHA);
	recursion_effect->inversed = obs_data_get_bool(settings, S_INVERSED);
	recursion_effect->reset_trigger =
		obs_data_get_int(settings, S_RESET_TRIGGER);
}

static void *recursion_effect_create(obs_data_t *settings, obs_source_t *source)
{
	struct recursion_effect_info *recursion_effect =
		bzalloc(sizeof(struct recursion_effect_info));
	recursion_effect->source = source;
	recursion_effect->hotkey = OBS_INVALID_HOTKEY_PAIR_ID;

	struct dstr filename = {0};
	dstr_cat(&filename, obs_get_module_data_path(obs_current_module()));
	dstr_cat(&filename, "/effects/render.effect");
	char *errors = NULL;
	obs_enter_graphics();
	recursion_effect->effect =
		gs_effect_create_from_file(filename.array, &errors);
	obs_leave_graphics();
	dstr_free(&filename);

	recursion_effect->param_image =
		gs_effect_get_param_by_name(recursion_effect->effect, "image");
	recursion_effect->param_multiplier =
		gs_effect_get_param_by_name(recursion_effect->effect, "multiplier");

	recursion_effect_update(recursion_effect, settings);
	return recursion_effect;
}

static void recursion_effect_destroy(void *data)
{
	struct recursion_effect_info *recursion_effect = data;
	if (recursion_effect->hotkey != OBS_INVALID_HOTKEY_PAIR_ID) {
		obs_hotkey_pair_unregister(recursion_effect->hotkey);
	}
	free_textures(recursion_effect);

	obs_enter_graphics();
	if (recursion_effect->effect)
		gs_effect_destroy(recursion_effect->effect);
	obs_leave_graphics();
	bfree(recursion_effect);
}

static void draw_frame(struct recursion_effect_info *f)
{
	struct frame frame;
	deque_peek_back(&f->frames, &frame, sizeof(frame));

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(frame.render);
	if (!tex)
		return;
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, tex);

	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(tex, 0, f->cx, f->cy);
	gs_blend_state_pop();
}

static void recursion_effect_video_render(void *data, gs_effect_t *effect)
{
	struct recursion_effect_info *f = data;

	obs_source_t *target = obs_filter_get_target(f->source);
	obs_source_t *parent = obs_filter_get_parent(f->source);

	if (!f->target_valid || !target || !parent || !f->frames.size) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	if (f->processed_frame) {
		draw_frame(f);
		return;
	}

	struct frame frame;
	deque_pop_front(&f->frames, &frame, sizeof(frame));

	gs_texrender_reset(f->render);

	gs_blend_state_push();
	if (f->inversed) {
		gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	} else {
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	}
	if (gs_texrender_begin(f->render, f->cx, f->cy)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)f->cx, 0.0f, (float)f->cy, -100.0f,
			 100.0f);
		if (f->inversed) {
			obs_source_video_render(target);
		}
		gs_texture_t *tex = gs_texrender_get_texture(frame.render);
		if (tex) {
			gs_matrix_push();
			gs_matrix_translate3f(f->offset.x, f->offset.y, 0.0f);
			gs_matrix_scale3f(f->scale.x, f->scale.y, 1.0f);
			gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, RAD(f->rotation));

			gs_effect_set_texture(f->param_image, tex);
			gs_effect_set_float(f->param_multiplier, f->alpha);

			while (gs_effect_loop(f->effect, "Draw"))
				gs_draw_sprite(tex, 0, f->cx, f->cy);
			gs_matrix_identity();
			gs_matrix_pop();
		}

		if (!f->inversed) {
			obs_source_video_render(target);
		}

		gs_texrender_end(f->render);
	}

	gs_blend_state_pop();

	gs_texrender_t *tmp = f->render;
	f->render = frame.render;
	frame.render = tmp;

	deque_push_back(&f->frames, &frame, sizeof(frame));
	draw_frame(f);
	f->processed_frame = true;

	UNUSED_PARAMETER(effect);
}

static inline bool check_size(struct recursion_effect_info *f)
{
	obs_source_t *target = obs_filter_get_target(f->source);

	f->target_valid = !!target;
	if (!f->target_valid)
		return true;

	const uint32_t cx = obs_source_get_base_width(target);
	const uint32_t cy = obs_source_get_base_height(target);

	f->target_valid = !!cx && !!cy;
	if (!f->target_valid)
		return true;

	if (cx != f->cx || cy != f->cy) {
		f->cx = cx;
		f->cy = cy;
		reset_textures(f);
		return true;
	}

	return false;
}

static void recursion_effect_tick(void *data, float t)
{
	UNUSED_PARAMETER(t);

	struct recursion_effect_info *f = data;

	f->processed_frame = false;
	if (f->reset_trigger == RESET_TRIGGER_ENABLE &&
	    !obs_source_enabled(f->source)) {
		f->interval_ns = 0;
		free_textures(f);
		return;
	}

	if (check_size(f))
		return;
	check_interval(f);
}

static obs_properties_t *recursion_effect_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p = obs_properties_add_int(
		props, S_DELAY_MS, obs_module_text("Delay"), 1, 1000, 1);
	obs_property_int_set_suffix(p, "ms");
	obs_properties_add_float_slider(props, S_OFFSET_X, obs_module_text("OffsetX"),
				 -1000.0, 1000.0, 1);
	obs_properties_add_float_slider(props, S_OFFSET_Y,
					obs_module_text("OffsetY"),
				 -1000.0, 1000.0, 1);
	obs_properties_add_float_slider(props, S_SCALE_X,
					obs_module_text("ScaleX"),
				 0.01, 10.0, 0.01);
	obs_properties_add_float_slider(props, S_SCALE_Y,
					obs_module_text("ScaleY"),
				 0.01, 10.0, 0.01);
	obs_properties_add_float_slider(props, S_ROTATION,
					obs_module_text("Rotation"),
				 -360.0, 360.0, 1.0);
	obs_properties_add_float_slider(props, S_ALPHA, obs_module_text("Alpha"),
				 0.001, 1.0, 0.001);
	obs_properties_add_bool(props, S_INVERSED, obs_module_text("Inversed"));

	p = obs_properties_add_list(props, S_RESET_TRIGGER,
				    obs_module_text("ResetTrigger"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("ResetTrigger.None"),
				  RESET_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("ResetTrigger.Show"),
				  RESET_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("ResetTrigger.Hide"),
				  RESET_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("ResetTrigger.Activate"),
				  RESET_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("ResetTrigger.Deactivate"),
				  RESET_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("ResetTrigger.Enable"),
				  RESET_TRIGGER_ENABLE);

	obs_properties_add_text(
		props, "plugin_info",
		"<a href=\"https://obsproject.com/forum/resources/recursion-effect.1008/\">Recursion Effect</a> (" PROJECT_VERSION
		") by <a href=\"https://www.exeldro.com\">Exeldro</a>",
		OBS_TEXT_INFO);

	return props;
}

void recursion_effect_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, S_SCALE_X, 1.0);
	obs_data_set_default_double(settings, S_SCALE_Y, 1.0);
	obs_data_set_default_double(settings, S_ALPHA, 1.0);
}

void recursion_effect_show(void *data)
{
	struct recursion_effect_info *f = data;
	if (f->reset_trigger == RESET_TRIGGER_SHOW)
		reset_textures(f);
}

void recursion_effect_hide(void *data)
{
	struct recursion_effect_info *f = data;
	if (f->reset_trigger == RESET_TRIGGER_HIDE)
		reset_textures(f);
}

void recursion_effect_activate(void *data)
{
	struct recursion_effect_info *f = data;
	if (f->reset_trigger == RESET_TRIGGER_ACTIVATE)
		reset_textures(f);
}

void recursion_effect_deactivate(void *data)
{
	struct recursion_effect_info *f = data;
	if (f->reset_trigger == RESET_TRIGGER_DEACTIVATE)
		reset_textures(f);
}

struct obs_source_info recursion_effect_filter = {
	.id = "recursion_effect_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB |
			OBS_SOURCE_CUSTOM_DRAW,
	.get_name = recursion_effect_get_name,
	.create = recursion_effect_create,
	.destroy = recursion_effect_destroy,
	.load = recursion_effect_update,
	.update = recursion_effect_update,
	.video_render = recursion_effect_video_render,
	.video_tick = recursion_effect_tick,
	.get_properties = recursion_effect_properties,
	.get_defaults = recursion_effect_defaults,
	.show = recursion_effect_show,
	.hide = recursion_effect_hide,
	.activate = recursion_effect_activate,
	.deactivate = recursion_effect_deactivate,
};

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("recursion-effect", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("RecursionEffect");
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Recursion Effect] loaded version %s", PROJECT_VERSION);
	obs_register_source(&recursion_effect_filter);
	return true;
}
