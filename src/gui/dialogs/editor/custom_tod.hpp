/*
   Copyright (C) 2008 - 2016 by Mark de Wever <koraq@xs4all.nl>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#ifndef GUI_DIALOGS_CUSTOM_TOD_HPP_INCLUDED
#define GUI_DIALOGS_CUSTOM_TOD_HPP_INCLUDED

#include "filechooser.hpp"
#include "time_of_day.hpp"
#include "gui/widgets/image.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/dialogs/dialog.hpp"
#include "editor/editor_display.hpp"

#include <vector>

namespace editor
{

class editor_display;

} // namespace editor

namespace gui2
{

class tlabel;
class timage;
class tslider;
class ttext_box;

class tcustom_tod : public tdialog
{
public:
	tcustom_tod(display& display,
				const std::vector<time_of_day>& tods);

	static bool execute(display& display, const std::vector<time_of_day>& tods)
	{
		return tcustom_tod(display, tods).show(display.video());
	}

private:
	/** Available time_of_days */
	std::vector<time_of_day> tods_;

	/** Callback for the next tod button */
	void do_next_tod(twindow& window);
	void do_prev_tod(twindow& window);

	void do_new_tod(twindow& window);
	void do_delete_tod(twindow& window);

	void select_file(const std::string& filename,
					 const std::string& default_dir,
					 const std::string& attribute,
					 twindow& window);

	void update_tod_display(twindow& window);

	void update_lawful_bonus(twindow& window);

	void set_selected_tod(time_of_day tod);
	const time_of_day& get_selected_tod() const;

	void update_selected_tod_info(twindow& window);

	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	/** Inherited from tdialog. */
	void pre_show(twindow& window);

	/** Inherited from tdialog. */
	void post_show(twindow& window);

	/** Current map generator index */
	int current_tod_;

	/** Text boxes for name and id*/
	ttext_box* current_tod_name_;
	ttext_box* current_tod_id_;

	/** Images for the current tod*/
	timage* current_tod_image_;
	timage* current_tod_mask_;

	/** Labels for the current tod*/
	tlabel* current_tod_sound_;
	tlabel* current_tod_number_;

	tfield_integer* lawful_bonus_field_;
	tslider* tod_red_field_;
	tslider* tod_green_field_;
	tslider* tod_blue_field_;

	/**
	 * The display to update when the ToD changes.
	 */
	display& display_;
};

} // namespace gui2

#endif
