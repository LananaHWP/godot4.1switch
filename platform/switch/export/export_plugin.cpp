/**************************************************************************/
/*  export_plugin.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "export_plugin.h"

#include "core/version.h"
#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "editor/editor_node.h"
#include "editor/editor_paths.h"
#include "editor/editor_scale.h"
#include "editor/export/editor_export.h"

#include "modules/modules_enabled.gen.h" // For svg.
#ifdef MODULE_SVG_ENABLED
#include "modules/svg/image_loader_svg.h"
#endif

Error EditorExportPlatformSwitch::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, int p_flags) {
	// what is happening:
	// first we create a normal .pck (no console wrapper / no pck embed fix)
	// we create a romfs folder, we put the image for the applet mode and the actual .pck in it
	// then we create the nacp file with game info provided, amd finally we create the nro + romfs (containing the pck)
	// the pck file got the exec_basename (see project_setting.cpp, the loading of the game need to match)
	// to build the nacp and the nro we use the devkitpro tools (they could de deployed in the platform template)

	// normally this is useless as they are hidden and set correctly by default
	p_preset->set("binary_format/embed_pck", false);
	p_preset->set("debug/export_console_wrapper", 0);

	String export_folder = p_path.get_base_dir();
	String base_name = p_path.get_basename().get_file();
	String nacp_file = base_name + ".nacp";
	String nacp_path = export_folder.path_join(nacp_file);
	String pck_file = base_name + ".pck";
	String pck_path = export_folder.path_join(pck_file);
	String nro_path = export_folder.path_join(base_name + ".nro");

	ERR_FAIL_COND_V_MSG(base_name.contains(" "), ERR_INVALID_DATA, "Export file \"" + base_name + "\" cannot contain space character.");

	String current_version = VERSION_FULL_CONFIG;
	String template_dir = EditorPaths::get_singleton()->get_export_templates_dir().path_join(current_version);

	String applet_splash_path = template_dir.path_join("switch").path_join("applet_splash.rgba.gz");
	String default_icon_path = template_dir.path_join("switch").path_join("icon.jpg");

	// Export project.
	Error err = EditorExportPlatformPC::export_project(p_preset, p_debug, p_path, p_flags);
	if (err != OK) {
		return err;
	}

	bool devprokit = p_preset->get("devkitpro/enabled");
	if (devprokit) {
		String bin_path = p_preset->get("devkitpro/tools_path");

		int exit_code = 0;
		{
			List<String> args;
			String output;

			String nacp_tool_path = bin_path.path_join(String(p_preset->get("devkitpro/nacp")));

			args.push_back("--create");
			args.push_back(p_preset->get("app/title"));
			args.push_back(p_preset->get("app/author"));
			args.push_back(p_preset->get("app/version"));
			args.push_back("\"" + nacp_path + "\"");

			err = OS::get_singleton()->execute(nacp_tool_path, args, &output, &exit_code, true);
			ERR_FAIL_COND_V_MSG(exit_code != 0, err, output);
			add_message(EXPORT_MESSAGE_INFO, TTR("Export"), TTR("nacp OK"));
		}

		// make the romfs temp dir
		const String romfs = export_folder.path_join("romfs");
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		da->remove(romfs);
		err = da->make_dir_recursive(romfs);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot create romfs");

		err = da->copy(pck_path, romfs.path_join(pck_file));
		ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot copy pck file");

		err = da->copy(applet_splash_path, romfs.path_join(applet_splash_path.get_file()));
		ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot copy splash file to romfs");

		//build the file nro
		{
			List<String> args;
			String output;
			String elf2nro_path = bin_path.path_join(String(p_preset->get("devkitpro/nro")));

			args.push_back(p_path);
			args.push_back(nro_path);
			args.push_back("--icon=\"" + default_icon_path + "\"");
			args.push_back("--nacp=\"" + nacp_path + "\"");
			args.push_back("--romfsdir=\"" + romfs + "\"");

			err = OS::get_singleton()->execute(elf2nro_path, args, &output, &exit_code, true);
			ERR_FAIL_COND_V_MSG(exit_code != 0, err, output);
			add_message(EXPORT_MESSAGE_INFO, TTR("Export"), TTR("elf2nro OK"));
		}

		//if nxlink is enabled send it
		if ((bool)(p_preset->get("nxlink/enabled"))) {
			List<String> args;
			String output;
			String nxlink_path = bin_path.path_join(String(p_preset->get("devkitpro/nxlink")));
			String address = String(p_preset->get("nxlink/address"));
			args.push_back("--address=" + address);
			args.push_back(nro_path);

			err = OS::get_singleton()->execute(nxlink_path, args, &output, &exit_code, true);
			ERR_FAIL_COND_V_MSG(exit_code != 0, err, output);
			add_message(EXPORT_MESSAGE_INFO, TTR("Export"), TTR("nxlink OK"));
		}
	}

	return OK;
}

String EditorExportPlatformSwitch::get_template_file_name(const String &p_target, const String &p_arch) const {
	return "switch_" + p_target + ".arm64";
}

List<String> EditorExportPlatformSwitch::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	list.push_back("arm64");
	return list;
}

bool EditorExportPlatformSwitch::get_export_option_visibility(const EditorExportPreset *p_preset, const String &p_option) const {
	if (p_preset) {
		// Hide devkitpro options.
		bool devprokit = p_preset->get("devkitpro/enabled");
		if (!devprokit && p_option != "devkitpro/enabled" && p_option.begins_with("devkitpro/")) {
			return false;
		}
		// Hide nxlink options.
		bool nxlink = p_preset->get("nxlink/enabled");
		if (!nxlink && p_option != "nxlink/enabled" && p_option.begins_with("nxlink/")) {
			return false;
		}
		// we do not support any of the following so hide them
		if (p_option == "binary_format/embed_pck") {
			return false;
		}
		if (p_option == "debug/export_console_wrapper") {
			return false;
		}
		if (p_option.begins_with("custom_template/")) {
			return false;
		}
	}
	return true;
}

void EditorExportPlatformSwitch::get_export_options(List<ExportOption> *r_options) const {
	EditorExportPlatformPC::get_export_options(r_options);

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "app/title"), "Godot Game"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "app/author"), "Author"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "app/version"), "1.0.0"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "app/icon"), "icon.jpg"));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "devkitpro/enabled"), true, true));
	//TODO:vrince deduce this
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "devkitpro/tools_path"), "/opt/devkitpro/tools/bin"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "devkitpro/nacp"), "nacptool"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "devkitpro/nro"), "elf2nro"));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "devkitpro/nxlink"), "nxlink"));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "nxlink/enabled"), false, true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "nxlink/address"), "0.0.0.0"));
}

String EditorExportPlatformSwitch::get_export_option_warning(const EditorExportPreset *p_preset, const StringName &p_name) const {
	if (p_preset) {
		//TODO test if devkits path exists
		return "";
	}
	return "";
}

Error EditorExportPlatformSwitch::fixup_embedded_pck(const String &p_path, int64_t p_embedded_start, int64_t p_embedded_size) {
	return OK;
}

Ref<Texture2D> EditorExportPlatformSwitch::get_run_icon() const {
	return run_icon;
}

bool EditorExportPlatformSwitch::poll_export() {
	return OK;
}

Ref<ImageTexture> EditorExportPlatformSwitch::get_option_icon(int p_index) const {
	return p_index == 1 ? stop_icon : EditorExportPlatform::get_option_icon(p_index);
}

int EditorExportPlatformSwitch::get_options_count() const {
	return menu_options;
}

String EditorExportPlatformSwitch::get_option_label(int p_index) const {
	return (p_index) ? TTR("Stop and uninstall") : TTR("Run on remote Linux/BSD system");
}

String EditorExportPlatformSwitch::get_option_tooltip(int p_index) const {
	return (p_index) ? TTR("Stop and uninstall running project from the remote system") : TTR("Run exported project on remote Linux/BSD system");
}

Error EditorExportPlatformSwitch::run(const Ref<EditorExportPreset> &p_preset, int p_device, int p_debug_flags) {
	EditorProgress ep("run", TTR("Running..."), 5);

	return OK;
}

EditorExportPlatformSwitch::EditorExportPlatformSwitch() {
	if (EditorNode::get_singleton()) {
#ifdef MODULE_SVG_ENABLED
		Ref<Image> img = memnew(Image);
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

		ImageLoaderSVG img_loader;
		img_loader.create_image_from_string(img, _switch_logo_svg, EDSCALE, upsample, false);
		set_logo(ImageTexture::create_from_image(img));

		img_loader.create_image_from_string(img, _switch_run_icon_svg, EDSCALE, upsample, false);
		run_icon = ImageTexture::create_from_image(img);
#endif

		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid()) {
			stop_icon = theme->get_icon(SNAME("Stop"), SNAME("EditorIcons"));
		} else {
			stop_icon.instantiate();
		}
	}
}
