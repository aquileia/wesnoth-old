/* $Id$ */
/*
   Copyright (C) 2003 - 2008 by David White <dave@whitevine.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2
   or at your option any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

/**
 *  @file unit.cpp
 *  Routines to manage units.
 */

#include "global.hpp"

#include "foreach.hpp"
#include "game_config.hpp"
#include "game_errors.hpp"
#include "game_preferences.hpp"
#include "gettext.hpp"
#include "log.hpp"
#include "pathfind.hpp"
#include "random.hpp"
#include "unit.hpp"
#include "unit_id.hpp"
#include "unit_types.hpp"
#include "unit_abilities.hpp"
#include "serialization/string_utils.hpp"
#include "halo.hpp"
#include "game_display.hpp"
#include "gamestatus.hpp"
#include "actions.hpp"
#include "game_events.hpp"
#include "sound.hpp"
#include "sdl_utils.hpp"
#include "terrain_filter.hpp"
#include "variable.hpp"
#include "callable_objects.hpp"
#include "formula.hpp"

#include <cassert>
#include <climits>
#include <ctime>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <iterator>

#define DBG_UT LOG_STREAM(debug, engine)
#define LOG_UT LOG_STREAM(info, engine)
#define ERR_UT LOG_STREAM(err, engine)
#define ERR_CONFIG LOG_STREAM(err, config)

namespace {
	const std::string ModificationTypes[] = { "advance", "trait", "object" };
	const size_t NumModificationTypes = sizeof(ModificationTypes)/
										sizeof(*ModificationTypes);

	/**
	 * Pointers to units which have data in their internal caches. The
	 * destructor of an unit removes itself from the cache, so the pointers are
	 * always valid.
	 */
	static std::vector<const unit *> units_with_cache;
}

static bool compare_unit_values(unit const &a, unit const &b)
{
	const int lvla = a.level();
	const int lvlb = b.level();

	const int xpa = a.max_experience() - a.experience();
	const int xpb = b.max_experience() - b.experience();

	return lvla > lvlb || (lvla == lvlb && xpa < xpb);
}

void sort_units(std::vector< unit > &units)
{
	std::sort(units.begin(), units.end(), compare_unit_values);
}

// Copy constructor
unit::unit(const unit& o):
           cfg_(o.cfg_),
           advances_to_(o.advances_to_),
           type_(o.type_),
           race_(o.race_),
           id_(o.id_),
           name_(o.name_),
           underlying_id_(o.underlying_id_),
           type_name_(o.type_name_),
           undead_variation_(o.undead_variation_),
           variation_(o.variation_),

           hit_points_(o.hit_points_),
           max_hit_points_(o.max_hit_points_),
           experience_(o.experience_),
           max_experience_(o.max_experience_),
           level_(o.level_),
           alignment_(o.alignment_),
           flag_rgb_(o.flag_rgb_),
           image_mods_(o.image_mods_),

           unrenamable_(o.unrenamable_),
           side_(o.side_),
           gender_(o.gender_),

           alpha_(o.alpha_),

           unit_formula_(o.unit_formula_),
           formula_vars_(o.formula_vars_ ? new game_logic::map_formula_callable(*o.formula_vars_) : o.formula_vars_),

           recruits_(o.recruits_),

           movement_(o.movement_),
           max_movement_(o.max_movement_),
           movement_costs_(o.movement_costs_),
           defense_mods_(o.defense_mods_),
           hold_position_(o.hold_position_),
           end_turn_(o.end_turn_),
           resting_(o.resting_),
           attacks_left_(o.attacks_left_),
           max_attacks_(o.max_attacks_),

           states_(o.states_),
           variables_(o.variables_),
           emit_zoc_(o.emit_zoc_),
           state_(o.state_),

           overlays_(o.overlays_),

           role_(o.role_),
           ai_special_(o.ai_special_),
           attacks_(o.attacks_),
           facing_(o.facing_),

           traits_description_(o.traits_description_),
           unit_value_(o.unit_value_),
           goto_(o.goto_),
           interrupted_move_(o.interrupted_move_),
           flying_(o.flying_),
           is_fearless_(o.is_fearless_),
           is_healthy_(o.is_healthy_),

           modification_descriptions_(o.modification_descriptions_),

           animations_(o.animations_),

           anim_(NULL),
		   next_idling_(0),

           frame_begin_time_(o.frame_begin_time_),
           unit_halo_(halo::NO_HALO),
           unit_anim_halo_(halo::NO_HALO),
           getsHit_(o.getsHit_),
           refreshing_(o.refreshing_),
           hidden_(o.hidden_),
           draw_bars_(o.draw_bars_),

           modifications_(o.modifications_),
           units_(o.units_),
           map_(o.map_),
           gamestatus_(o.gamestatus_),
		   invisibility_cache_()
{
}

unit::unit(unit_map* unitmap, const gamemap* map, const gamestatus* game_status,
		const std::vector<team>* /*teams*/,const config& cfg,
		bool use_traits, game_state* state) :
	cfg_(),
	advances_to_(),
	type_(),
	race_(NULL),
	id_(),
	name_(),
	underlying_id_(0),
	type_name_(),
	undead_variation_(),
	variation_(),
	hit_points_(0),
	max_hit_points_(0),
	experience_(0),
	max_experience_(0),
	level_(0),
	alignment_(),
	flag_rgb_(),
	image_mods_(),
	unrenamable_(false),
	side_(0),
	gender_(),
	alpha_(),
	unit_formula_(),
	formula_vars_(),
	recruits_(),
	movement_(0),
	max_movement_(0),
	movement_costs_(),
	defense_mods_(),
	hold_position_(false),
	end_turn_(false),
	resting_(false),
	attacks_left_(0),
	max_attacks_(0),
	states_(),
	variables_(),
	emit_zoc_(0),
	state_(STATE_STANDING),
	overlays_(),
	role_(),
	ai_special_(),
	attacks_(),
	facing_(gamemap::location::SOUTH_EAST),
	traits_description_(),
	unit_value_(),
	goto_(),
	interrupted_move_(),
	flying_(false),
	is_fearless_(false),
	is_healthy_(false),
	modification_descriptions_(),
	animations_(),
	anim_(NULL),
	next_idling_(0),
	frame_begin_time_(0),
	unit_halo_(halo::NO_HALO),
	unit_anim_halo_(halo::NO_HALO),
	getsHit_(0),
	refreshing_(false),
	hidden_(false),
	draw_bars_(false),
	modifications_(),
	units_(unitmap),
	map_(map),
	gamestatus_(game_status),
	invisibility_cache_()
{
	read(cfg, use_traits, state);
	/** @todo Are these modified by read? if not they can be removed. */
	getsHit_=0;
	end_turn_ = false;
	refreshing_  = false;
	hidden_ = false;
	game_config::add_color_info(cfg);
}

unit::unit(const config& cfg,bool use_traits) :
	cfg_(),
	advances_to_(),
	type_(),
	race_(NULL),
	id_(),
	name_(),
	underlying_id_(0),
	type_name_(),
	undead_variation_(),
	variation_(),
	hit_points_(0),
	max_hit_points_(0),
	experience_(0),
	max_experience_(0),
	level_(0),
	alignment_(),
	flag_rgb_(),
	image_mods_(),
	unrenamable_(false),
	side_(0),
	gender_(),
	alpha_(),
	unit_formula_(),
	formula_vars_(),
	recruits_(),
	movement_(0),
	max_movement_(0),
	movement_costs_(),
	defense_mods_(),
	hold_position_(false),
	end_turn_(false),
	resting_(false),
	attacks_left_(0),
	max_attacks_(0),
	states_(),
	variables_(),
	emit_zoc_(0),
	state_(STATE_STANDING),
	overlays_(),
	role_(),
	ai_special_(),
	attacks_(),
	facing_(gamemap::location::SOUTH_EAST),
	traits_description_(),
	unit_value_(),
	goto_(),
	interrupted_move_(),
	flying_(false),
	is_fearless_(false),
	is_healthy_(false),
	modification_descriptions_(),
	animations_(),
	anim_(NULL),
	next_idling_(0),
	frame_begin_time_(0),
	unit_halo_(halo::NO_HALO),
	unit_anim_halo_(halo::NO_HALO),
	getsHit_(0),
	refreshing_(false),
	hidden_(false),
	draw_bars_(false),
	modifications_(),
	units_(NULL),
	map_(NULL),
	gamestatus_(NULL),
	invisibility_cache_()
{
	read(cfg,use_traits);
	/** @todo Are these modified by read? if not they can be removed. */
	getsHit_=0;
	end_turn_ = false;
	refreshing_ = false;
	hidden_ = false;
}

void unit::clear_status_caches()
{
	for(std::vector<const unit *>::const_iterator itor = units_with_cache.begin();
			itor != units_with_cache.end(); ++itor) {
		(*itor)->clear_visibility_cache();
	}

	units_with_cache.clear();
}

unit_race::GENDER unit::generate_gender(const unit_type& type, bool gen, game_state* state)
{
	const std::vector<unit_race::GENDER>& genders = type.genders();
	// Once random gender is used, don't do it again.
	// Such as when restoring a saved character.
	cfg_["random_gender"] = "no";
	if(genders.empty() == false) {
		if(state) {
			return gen ? genders[state->rng().get_random() % genders.size()] : genders.front();
		} else {
			return gen ? genders[get_random() % genders.size()] : genders.front();
		}
	} else {
		return unit_race::MALE;
	}
}

unit::unit(unit_map* unitmap, const gamemap* map, const gamestatus* game_status,
		const std::vector<team>* /*teams*/, const unit_type* t, int side,
		bool use_traits, bool dummy_unit, unit_race::GENDER gender, std::string variation) :
	cfg_(),
	advances_to_(),
	type_(),
	race_(NULL),
	id_(),
	name_(),
	underlying_id_(0),
	type_name_(),
	undead_variation_(),
	variation_(variation),
	hit_points_(0),
	max_hit_points_(0),
	experience_(0),
	max_experience_(0),
	level_(0),
	alignment_(),
	flag_rgb_(),
	image_mods_(),
	unrenamable_(false),
	side_(side),
	gender_(dummy_unit ? gender : generate_gender(*t,use_traits)),
	alpha_(),
	unit_formula_(),
	formula_vars_(),
	recruits_(),
	movement_(0),
	max_movement_(0),
	movement_costs_(),
	defense_mods_(),
	hold_position_(false),
	end_turn_(false),
	resting_(false),
	attacks_left_(0),
	max_attacks_(0),
	states_(),
	variables_(),
	emit_zoc_(0),
	state_(STATE_STANDING),
	overlays_(),
	role_(),
	ai_special_(),
	attacks_(),
	facing_(gamemap::location::SOUTH_EAST),
	traits_description_(),
	unit_value_(),
	goto_(),
	interrupted_move_(),
	flying_(false),
	is_fearless_(false),
	is_healthy_(false),
	modification_descriptions_(),
	animations_(),
	anim_(NULL),
	next_idling_(0),
	frame_begin_time_(0),
	unit_halo_(halo::NO_HALO),
	unit_anim_halo_(halo::NO_HALO),
	getsHit_(0),
	refreshing_(false),
	hidden_(false),
	draw_bars_(false),
	modifications_(),
	units_(unitmap),
	map_(map),
	gamestatus_(game_status),
	invisibility_cache_()
{
	cfg_["upkeep"]="full";
	advance_to(t);
	if(dummy_unit == false) validate_side(side_);
	if(use_traits) {
		// Units that don't have traits generated are just
		// generic units, so they shouldn't get a description
		// either.
		name_ = generate_name();
	}
	generate_traits(!use_traits);
	reset_modifications();
	apply_modifications();
	set_underlying_id();

	/**
	 * @todo Test whether the calls above modify these values if not they can
	 * removed, since already set in the initialization list.
	 */
	unrenamable_ = false;
	anim_ = NULL;
	getsHit_=0;
	end_turn_ = false;
	hold_position_ = false;
	next_idling_ = 0;
	frame_begin_time_ = 0;
	unit_halo_ = halo::NO_HALO;
	unit_anim_halo_ = halo::NO_HALO;
}

unit::unit(const unit_type* t, int side, bool use_traits, bool dummy_unit,
		unit_race::GENDER gender, std::string variation) :
	cfg_(),
	advances_to_(),
	type_(),
	race_(NULL),
	id_(),
	name_(),
	underlying_id_(0),
	type_name_(),
	undead_variation_(),
	variation_(variation),
	hit_points_(0),
	max_hit_points_(0),
	experience_(0),
	max_experience_(0),
	level_(0),
	alignment_(),
	flag_rgb_(),
	image_mods_(),
	unrenamable_(false),
	side_(side),
	gender_(dummy_unit ? gender : generate_gender(*t,use_traits)),
	alpha_(),
	unit_formula_(),
	formula_vars_(),
	recruits_(),
	movement_(0),
	max_movement_(0),
	movement_costs_(),
	defense_mods_(),
	hold_position_(false),
	end_turn_(false),
	resting_(false),
	attacks_left_(0),
	max_attacks_(0),
	states_(),
	variables_(),
	emit_zoc_(0),
	state_(STATE_STANDING),
	overlays_(),
	role_(),
	ai_special_(),
	attacks_(),
	facing_(gamemap::location::SOUTH_EAST),
	traits_description_(),
	unit_value_(),
	goto_(),
	interrupted_move_(),
	flying_(false),
	is_fearless_(false),
	is_healthy_(false),
	modification_descriptions_(),
	animations_(),
	anim_(NULL),
	next_idling_(0),
	frame_begin_time_(0),
	unit_halo_(halo::NO_HALO),
	unit_anim_halo_(halo::NO_HALO),
	getsHit_(0),
	refreshing_(false),
	hidden_(false),
	draw_bars_(false),
	modifications_(),
	units_(NULL),
	map_(NULL),
	gamestatus_(NULL),
	invisibility_cache_()
{
	cfg_["upkeep"]="full";
	advance_to(t);
	if(dummy_unit == false) validate_side(side_);
	if(use_traits) {
		// Units that don't have traits generated are just
		// generic units, so they shouldn't get a description
		// either.
		name_ = generate_name();
	}
	generate_traits(!use_traits);
	reset_modifications();
	apply_modifications();
	set_underlying_id();

	/**
	 * @todo Test whether the calls above modify these values if not they can
	 * removed, since already set in the initialization list.
	 */
	unrenamable_ = false;
	next_idling_ = 0;
	frame_begin_time_ = 0;
	anim_ = NULL;
	getsHit_=0;
	end_turn_ = false;
	hold_position_ = false;
}

unit::~unit()
{
	clear_haloes();

	delete anim_;

	// Remove us from the status cache
	std::vector<const unit *>::iterator itor =
	std::find(units_with_cache.begin(), units_with_cache.end(), this);

	if(itor != units_with_cache.end()) {
		units_with_cache.erase(itor);
	}
}



unit& unit::operator=(const unit& u)
{
	// Use copy constructor to make sure we are coherant
	if (this != &u) {
		this->~unit();
		new (this) unit(u) ;
	}
	return *this ;
}



void unit::set_game_context(unit_map* unitmap, const gamemap* map, const gamestatus* game_status, const std::vector<team>* /*teams*/)
{
	units_ = unitmap;
	map_ = map;
	gamestatus_ = game_status;

	// In case the unit carries EventWML, apply it now
	game_events::add_events(cfg_.get_children("event"),type_);
}

// Apply mandatory traits (e.g. undead, mechanical) to a unit and then
// fill out with avaiable (leaders have a restircted set of available traits)
// traits until no more are available or the unit has its maximum number
// of traits.
// This routine does not apply the effects of added traits to a unit.
// That must be done by the caller.
// Note that random numbers used in config files don't work in multiplayer,
// so that leaders should be barred from all random traits until that
// is fixed. Later the restrictions will be based on play balance.
// musthavepnly is true when you don't want to generate random traits or
// you don't want to give any optional traits to a unit.

void unit::generate_traits(bool musthaveonly, game_state* state)
{
	LOG_UT << "Generating a trait for unit type " << type_id() << " with musthaveonly " << musthaveonly << "\n";
	const unit_type_data::unit_type_map::const_iterator type = unit_type_data::types().find(type_id());
	// Calculate the unit's traits
	if (type == unit_type_data::types().end()) {
		std::string error_message = _("Unknown unit type '$type|' while generating traits");
		utils::string_map symbols;
		symbols["type"] = type_id();
		error_message = utils::interpolate_variables_into_string(error_message, &symbols);
		LOG_STREAM(err, engine) << "unit of type " << type_id() << " not found!\n";
		throw game::game_error(error_message);
	}
	std::vector<config*> candidate_traits = type->second.possible_traits();
	std::vector<config*> traits;

	// First remove traits the unit already has from consideration.
	// And count them so that we can figure out how many more are needed.
	size_t t = 0;
	config::child_list const &mods = modifications_.get_children("trait");
	for(config::child_list::const_iterator j = mods.begin(), j_end = mods.end(); j != j_end; ++j) {
		++t;
		size_t m = 0;
		for(size_t n = 0; n < candidate_traits.size(); ++n) {
			if((**(candidate_traits.begin()+m))["id"] == (**j)["id"]) {
				candidate_traits.erase(candidate_traits.begin()+m);
			} else {
				++m;
			}
		}
	}

	// Next add in any mandatory traits. These aren't limited by the
	// number of traits allowed for a unit. They also don't use
	// any random numbers for assignment. (And hence don't cause
	// problems for multiplayer.)
	size_t num_traits = candidate_traits.size();
	size_t m = 0;
	for(size_t n = 0; n < num_traits; ++n) {
		if(!(**(candidate_traits.begin()+m))["availability"].empty() &&
		(**(candidate_traits.begin()+m))["availability"] == "musthave") {
			traits.push_back(candidate_traits[m]);
			candidate_traits.erase(candidate_traits.begin()+m);
			++t;
		} else {
			++m;
		}
	}

	// If musthaveonly then don't generate any random/optional traits
	if(!musthaveonly) {
		// Next for leaders remove any traits that are not available to
		// the "any" category.
		if(can_recruit()) {
			num_traits = candidate_traits.size();
			m = 0;
			for(size_t n = 0; n < num_traits; ++n) {
				if(!(**(candidate_traits.begin()+m))["availability"].empty() ||
				(**(candidate_traits.begin()+m))["availability"] != "any") {
					candidate_traits.erase(candidate_traits.begin()+m);
				} else {
					++m;
				}
			}
		}

		// Now randomly fill out to the number of traits required or until
		// there aren't any more traits.
		num_traits = type->second.num_traits();
		for(size_t n = t; n < num_traits && candidate_traits.empty() == false; ++n) {
			const size_t num =
				(state ?  state->rng().get_random() : get_random()) % candidate_traits.size();
			traits.push_back(candidate_traits[num]);
			candidate_traits.erase(candidate_traits.begin()+num);
		}

		// Once random traits are added, don't do it again.
		// Such as when restoring a saved character.
		cfg_["random_traits"]="no";
	}

	for(std::vector<config*>::const_iterator j2 = traits.begin(); j2 != traits.end(); ++j2) {
		modifications_.add_child("trait",**j2);
	}
}

std::vector<std::string> unit::get_traits_list() const
{
	std::vector<std::string> res;

	config::child_list const &mods = modifications_.get_children("trait");
	for(config::child_list::const_iterator j = mods.begin(), j_end = mods.end(); j != j_end; ++j)
	{
			std::string const &id = (**j)["id"];
			if (!id.empty())
				res.push_back(id);
	}
	return res;
}

void unit::advance_to(const unit_type* t, bool use_traits, game_state* state)
{
	t = &t->get_gender_unit_type(gender_).get_variation(variation_);
	reset_modifications();

	// Remove old type's halo, animations, abilities, and attacks
	cfg_["halo"] = "";
	foreach(const std::string& tag_name, unit_animation::all_tag_names()) {
		cfg_.clear_children(tag_name);
	}
	cfg_.clear_children("abilities");
	cfg_.clear_children("attacks");

	if(t->movement_type().get_parent()) {
		cfg_.merge_with(t->movement_type().get_parent()->get_cfg());
	}
	// If unit has specific profile, remember it and keep it after advancing
	std::string specific_profile;
	if (type() != NULL)	{
		const std::string profile = cfg_["profile"];
		if (!profile.empty() && profile != type()->cfg_["profile"]){
			specific_profile = profile;
		}
	}

	cfg_.merge_with(t->cfg_);
	if (!specific_profile.empty()) {
		cfg_["profile"] = specific_profile;
	}
	cfg_.clear_children("male");
	cfg_.clear_children("female");

	advances_to_ = t->advances_to();
	cfg_.values.erase("advanceto");

	race_ = t->race_;
	type_name_ = t->type_name();
	cfg_["description"] = t->unit_description();
	undead_variation_ = t->undead_variation();
	max_experience_ = t->experience_needed(false);
	level_ = t->level();
	alignment_ = t->alignment();
	alpha_ = t->alpha();
	hit_points_ = t->hitpoints();
	max_hit_points_ = t->hitpoints();
	max_movement_ = t->movement();
	emit_zoc_ = t->has_zoc();
	attacks_ = t->attacks();
	unit_value_ = t->cost();
	flying_ = t->movement_type().is_flying();

	max_attacks_ = t->max_attacks();

	animations_ = t->animations();

	flag_rgb_ = t->flag_rgb();


	bool do_heal = false; // Track whether unit should get fully healed.

	if(utils::string_bool(cfg_["random_gender"], false)) {
		cfg_["gender"] = gender_string(generate_gender(*t,true));
	}

	if(type_id()!=t->id()) {
		do_heal = true; // Can't heal until after mods applied.
		type_ = t->id();
	}

	if(utils::string_bool(cfg_["random_traits"], true)) {
		generate_traits(!use_traits, state);
	} else {
		// This will add any "musthave" traits to the new unit that it doesn't already have.
		// This covers the Dark Sorcerer advancing to Lich and gaining the "undead" trait,
		// but random and/or optional traits are not added,
		// and neither are inappropiate traits removed.
		generate_traits(true);
	}

	// Apply modifications etc, refresh the unit.
	// This needs to be after type and gender are fixed,
	// since there can be filters on the modifications
	// that may result in different effects after the advancement.
	apply_modifications();

	// Not that the unit has all of its modifications applied, it is
	// OK to heal it.
	if (do_heal) {
		heal_all();
	}

	game_events::add_events(cfg_.get_children("event"),type_);

	set_state("poisoned","");
	set_state("slowed","");
	set_state("stoned","");
	end_turn_ = false;
	refreshing_  = false;
	hidden_ = false;
}

const unit_type* unit::type() const
{
	std::map<std::string,unit_type>::const_iterator i = unit_type_data::types().find(type_id());
	if(i != unit_type_data::types().end()) {
		return &i->second.get_gender_unit_type(gender_).get_variation(variation_);
	}
	if (!type_id().empty()) {
		std::string error_message = _("Unknown unit type '$type|'");
		utils::string_map symbols;
		symbols["type"] = type_id();
		error_message = utils::interpolate_variables_into_string(error_message, &symbols);
		LOG_STREAM(err, engine) << "unit of type " << type_id() << " not found!\n";
		throw game::game_error(error_message);
	}
	return NULL;
}

const std::string& unit::profile() const
{
	if(cfg_["profile"] != "" && cfg_["profile"] != "unit_image") {
		return cfg_["profile"];
	}
	return absolute_image();
}

SDL_Colour unit::hp_color() const
{
	double unit_energy = 0.0;
	SDL_Color energy_colour = {0,0,0,0};

	if(max_hitpoints() > 0) {
		unit_energy = double(hitpoints())/double(max_hitpoints());
	}

	if(1.0 == unit_energy){
		energy_colour.r = 33;
		energy_colour.g = 225;
		energy_colour.b = 0;
	} else if(unit_energy > 1.0) {
		energy_colour.r = 100;
		energy_colour.g = 255;
		energy_colour.b = 100;
	} else if(unit_energy >= 0.75) {
		energy_colour.r = 170;
		energy_colour.g = 255;
		energy_colour.b = 0;
	} else if(unit_energy >= 0.5) {
		energy_colour.r = 255;
		energy_colour.g = 155;
		energy_colour.b = 0;
	} else if(unit_energy >= 0.25) {
		energy_colour.r = 255;
		energy_colour.g = 175;
		energy_colour.b = 0;
	} else {
		energy_colour.r = 255;
		energy_colour.g = 0;
		energy_colour.b = 0;
	}
	return energy_colour;
}

SDL_Colour unit::xp_color() const
{
	const SDL_Color near_advance_colour = {255,255,255,0};
	const SDL_Color mid_advance_colour  = {150,255,255,0};
	const SDL_Color far_advance_colour  = {0,205,205,0};
	const SDL_Color normal_colour 	  = {0,160,225,0};
	const SDL_Color near_amla_colour	  = {225,0,255,0};
	const SDL_Color mid_amla_colour	  = {169,30,255,0};
	const SDL_Color far_amla_colour	  = {139,0,237,0};
	const SDL_Color amla_colour		  = {100,0,150,0};
	const bool near_advance = max_experience() - experience() <= game_config::kill_experience;
	const bool mid_advance  = max_experience() - experience() <= game_config::kill_experience*2;
	const bool far_advance  = max_experience() - experience() <= game_config::kill_experience*3;

	SDL_Color colour=normal_colour;
	if(advances_to().size()){
		if(near_advance){
			colour=near_advance_colour;
		} else if(mid_advance){
			colour=mid_advance_colour;
		} else if(far_advance){
			colour=far_advance_colour;
		}
	} else if (get_modification_advances().size()){
		if(near_advance){
			colour=near_amla_colour;
		} else if(mid_advance){
			colour=mid_amla_colour;
		} else if(far_advance){
			colour=far_amla_colour;
		} else {
			colour=amla_colour;
		}
	}
	return(colour);
}

void unit::set_movement(int moves)
{
	hold_position_ = false;
	end_turn_ = false;
	movement_ = std::max<int>(0,std::min<int>(moves,max_movement_));
}

void unit::new_turn()
{
	end_turn_ = false;
	movement_ = total_movement();
	attacks_left_ = max_attacks_;
	set_state("hidden","yes");

	if (hold_position_) {
		end_turn_ = true;
	}
}
void unit::end_turn()
{
	set_state("slowed","");
	if((movement_ != total_movement()) && !utils::string_bool(get_state("not_moved")) && (!is_healthy_ || attacks_left_ < max_attacks_)) {
		resting_ = false;
	}
	set_state("not_moved","");
	// Clear interrupted move
	set_interrupted_move(gamemap::location());
}
void unit::new_level()
{
	ai_special_ = "";

	// Set the goto-command to be going to no-where
	goto_ = gamemap::location();

	remove_temporary_modifications();

	// Re-apply all permanent modifications
	reset_modifications();
	apply_modifications();

	heal_all();
	set_state("slowed","");
	set_state("poisoned","");
	set_state("stoned","");
}
void unit::remove_temporary_modifications()
{
	for(unsigned int i = 0; i != NumModificationTypes; ++i) {
		const std::string& mod = ModificationTypes[i];
		const config::child_list& mods = modifications_.get_children(mod);
		for(size_t j = 0; j != mods.size(); ++j) {
			if((*mods[j])["duration"] != "forever" && (*mods[j])["duration"] != "") {
				modifications_.remove_child(mod,j);
				--j;
			}
		}
	}
}

void unit::heal(int amount)
{
	int max_hp = max_hitpoints();
	if (hit_points_ < max_hp) {
		hit_points_ += amount;
		if (hit_points_ > max_hp) {
			hit_points_ = max_hp;
		}
	}
	if(hit_points_<1) {
		hit_points_ = 1;
	}
}

std::string unit::get_state(const std::string& state) const
{
	std::map<std::string,std::string>::const_iterator i = states_.find(state);
	if(i != states_.end()) {
		return i->second;
	}
	return "";
}
void unit::set_state(const std::string& state, const std::string& value)
{
	if(value == "") {
		std::map<std::string,std::string>::iterator i = states_.find(state);
		if(i != states_.end()) {
			states_.erase(i);
		}
	} else {
		states_[state] = value;
	}
}


bool unit::has_ability_by_id(const std::string& ability) const
{
	const config* abil = cfg_.child("abilities");
	if(abil) {
		for(config::child_map::const_iterator i = abil->all_children().begin(); i != abil->all_children().end(); ++i) {
			for(config::child_list::const_iterator j = i->second.begin(); j != i->second.end(); ++j) {
				if((**j)["id"] == ability) {
					return true;
				}
			}
		}
	}
	return false;
}

void unit::remove_ability_by_id(const std::string &ability)
{
	config* abil = cfg_.child("abilities");
	if(abil) {
		config::all_children_iterator i = abil->ordered_begin();
		while(i != abil->ordered_end()) {
			if(i.get_child()["id"] == ability) {
				i = abil->erase(i);
			} else {
				++i;
			}
		}
	}
}

bool unit::matches_filter(const vconfig& cfg, const gamemap::location& loc, bool use_flat_tod) const
{
	bool matches = true;

	if(loc.valid()) {
		assert(units_ != NULL);
		scoped_xy_unit auto_store("this_unit", loc.x, loc.y, *units_);
		matches = internal_matches_filter(cfg, loc, use_flat_tod);
	} else {
		// If loc is invalid, then this is a recall list unit (already been scoped)
		matches = internal_matches_filter(cfg, loc, use_flat_tod);
	}

	// Handle [and], [or], and [not] with in-order precedence
	vconfig::all_children_iterator cond = cfg.ordered_begin();
	vconfig::all_children_iterator cond_end = cfg.ordered_end();
	while(cond != cond_end)
	{

		const std::string& cond_name = cond.get_key();
		const vconfig& cond_filter = cond.get_child();

		// Handle [and]
		if(cond_name == "and") {
			matches = matches && matches_filter(cond_filter,loc,use_flat_tod);
		}
		// Handle [or]
		else if(cond_name == "or") {
			matches = matches || matches_filter(cond_filter,loc,use_flat_tod);
		}
		// Handle [not]
		else if(cond_name == "not") {
			matches = matches && !matches_filter(cond_filter,loc,use_flat_tod);
		}

		++cond;
	}
	return matches;
}

bool unit::internal_matches_filter(const vconfig& cfg, const gamemap::location& loc, bool use_flat_tod) const
{
	const t_string& t_id = cfg["id"];
	const t_string& t_description = cfg["description"];
	const t_string& t_speaker = cfg["speaker"];
	const t_string& t_type = cfg["type"];
	const t_string& t_ability = cfg["ability"];
	const t_string& t_side = cfg["side"];
	const t_string& t_weapon = cfg["has_weapon"];
	const t_string& t_role = cfg["role"];
	const t_string& t_ai_special = cfg["ai_special"];
	const t_string& t_race = cfg["race"];
	const t_string& t_gender = cfg["gender"];
	const t_string& t_canrecruit = cfg["canrecruit"];
	const t_string& t_level = cfg["level"];
	const t_string& t_defense = cfg["defense"];
	const t_string& t_movement_cost = cfg["movement_cost"];

	const std::string& id = t_id;
	const std::string& description = t_description;
	const std::string& speaker = t_speaker;
	const std::string& type = t_type;
	const std::string& ability = t_ability;
	const std::string& side = t_side;
	const std::string& weapon = t_weapon;
	const std::string& role = t_role;
	const std::string& ai_special = t_ai_special;
	const std::string& race = t_race;
	const std::string& gender = t_gender;
	const std::string& canrecruit = t_canrecruit;
	const std::string& level = t_level;
	const std::string& defense = t_defense;
	const std::string& mvt_cost = t_movement_cost;

	if(description.empty() == false && description != name_) {
		return false;
	}

	if(id.empty() == false && id != this->id()) {
		return false;
	}

	// Allow 'speaker' as an alternative to id, since people use it so often
	if(speaker.empty() == false && speaker != this->id()) {
		return false;
	}

	if(cfg.has_child("filter_location")) {
		assert(map_ != NULL);
		assert(gamestatus_ != NULL);
		assert(units_ != NULL);
		const vconfig& t_cfg = cfg.child("filter_location");
		terrain_filter t_filter(t_cfg, *map_, *gamestatus_, *units_, use_flat_tod);
		if(!t_filter.match(loc)) {
			return false;
		}
	}
	// Also allow filtering on location ranges outside of the location filter
	const t_string& cfg_x = cfg["x"];
	const t_string& cfg_y = cfg["y"];
	if(!cfg_x.empty() || !cfg_y.empty()){
		if(cfg_x == "recall" && cfg_y == "recall") {
			//locations on the map are considered to not be on a recall list
			if((!map_ && loc.valid()) || (map_ && map_->on_board(loc))) {
				return false;
			}
		} else if(!loc.matches_range(cfg_x, cfg_y)) {
			return false;
		}
	}

	const std::string& this_type = type_id();

	// The type could be a comma separated list of types
	if(type.empty() == false && type != this_type) {

		// We only do the full CSV search if we find a comma in there,
		// and if the subsequence is found within the main sequence.
		// This is because doing the full CSV split is expensive.
		if(std::find(type.begin(),type.end(),',') != type.end() &&
		   std::search(type.begin(),type.end(),this_type.begin(),
					   this_type.end()) != type.end()) {
			const std::vector<std::string>& vals = utils::split(type);

			if(std::find(vals.begin(),vals.end(),this_type) == vals.end()) {
				return false;
			}
		} else {
			return false;
		}
	}

	if(ability.empty() == false && has_ability_by_id(ability) == false) {
		if(std::find(ability.begin(),ability.end(),',') != ability.end()) {
			const std::vector<std::string>& vals = utils::split(ability);
			bool has_ability = false;
			for(std::vector<std::string>::const_iterator this_ability = vals.begin(); this_ability != vals.end(); ++this_ability) {
				if(has_ability_by_id(*this_ability)) {
					has_ability = true;
					break;
				}
			}
			if(!has_ability) {
				return false;
			}
		} else {
			return false;
		}
	}

	if(race.empty() == false && race_->id() != race) {
		return false;
	}

	if(gender.empty() == false) {
		if(string_gender(gender) != this->gender()) {
			return false;
		}
	}

	if(side.empty() == false && this->side() != lexical_cast_default<unsigned>(side)) {
		if(std::find(side.begin(),side.end(),',') != side.end()) {
			const std::vector<std::string>& vals = utils::split(side);

			std::ostringstream s;
			s << (this->side());
			if(std::find(vals.begin(),vals.end(),s.str()) == vals.end()) {
				return false;
			}
		} else {
			return false;
		}
	  }

	if(weapon.empty() == false) {
		bool has_weapon = false;
		const std::vector<attack_type>& attacks = this->attacks();
		for(std::vector<attack_type>::const_iterator i = attacks.begin();
			i != attacks.end(); ++i) {
			if(i->id() == weapon) {
				has_weapon = true;
			}
		}

		if(!has_weapon) {
			return false;
		}
	}

	if(role.empty() == false && role_ != role) {
		return false;
	}

	if(ai_special.empty() == false && ai_special_ != ai_special) {
		return false;
	}

	if(canrecruit.empty() == false && utils::string_bool(canrecruit) != can_recruit()) {
		return false;
	}

	if(level.empty() == false && level_ != lexical_cast_default<int>(level,-1)) {
		return false;
	}

	if(defense.empty() == false && defense_modifier(map_->get_terrain(loc)) != lexical_cast_default<int>(defense,-1)) {
		return false;
	}

	if(mvt_cost.empty() == false && movement_cost(map_->get_terrain(loc)) != lexical_cast_default<int>(mvt_cost,-1)) {
		return false;
	}

	// Now start with the new WML based comparison.
	// If a key is in the unit and in the filter, they should match
	// filter only => not for us
	// unit only => not filtered
	const vconfig::child_list& wmlcfgs = cfg.get_children("filter_wml");
	if (!wmlcfgs.empty()) {
		config unit_cfg;
		write(unit_cfg);
		// Now, match the kids, WML based
		for(unsigned int i=0; i < wmlcfgs.size(); ++i) {
			if(!unit_cfg.matches(wmlcfgs[i].get_parsed_config())) {
				return false;
			}
		}
	}

	if (cfg.has_child("filter_vision")) {
		const vconfig::child_list& vis_filt = cfg.get_children("filter_vision");
		vconfig::child_list::const_iterator i, i_end = vis_filt.end();
		for (i = vis_filt.begin(); i != i_end; ++i) {
			bool visible = utils::string_bool((*i)["visible"], true);
			std::set<int> viewers;
			if (i->has_attribute("viewing_side")) {
				std::vector<std::pair<int,int> > ranges = utils::parse_ranges((*i)["viewing_side"]);
				std::vector<std::pair<int,int> >::const_iterator range, range_end = ranges.end();
				for (range = ranges.begin(); range != range_end; ++range) {
					for (int i=range->first; i<=range->second; ++i) {
						if (i > 0 && static_cast<size_t>(i) <= teams_manager::get_teams().size()) {
							viewers.insert(i);
						}
					}
				}
			} else {
				//if viewing_side is not defined, default to all enemies
				const team& my_team = teams_manager::get_teams()[this->side()-1];
				for (size_t i = 1; i <= teams_manager::get_teams().size(); ++i) {
					if (my_team.is_enemy(i)) {
						viewers.insert(i);
					}
				}
			}
			if (viewers.empty()) {
				return false;
			}
			std::set<int>::const_iterator viewer, viewer_end = viewers.end();
			for (viewer = viewers.begin(); viewer != viewer_end; ++viewer) {
				bool not_fogged = !teams_manager::get_teams()[*viewer - 1].fogged(loc);
				bool not_hiding = !this->invisible(loc, *units_, teams_manager::get_teams()/*, false(?) */);
				if (visible != not_fogged && not_hiding) {
					return false;
				}
			}
		}
	}

	if (cfg.has_child("filter_adjacent")) {
		assert(units_ && map_ && gamestatus_);
		gamemap::location adjacent[6];
		get_adjacent_tiles(loc, adjacent);
		vconfig::child_list::const_iterator i, i_end;
		const vconfig::child_list& adj_filt = cfg.get_children("filter_adjacent");
		for (i = adj_filt.begin(), i_end = adj_filt.end(); i != i_end; ++i) {
			int match_count=0;
			static std::vector<gamemap::location::DIRECTION> default_dirs
				= gamemap::location::parse_directions("n,ne,se,s,sw,nw");
			std::vector<gamemap::location::DIRECTION> dirs = (*i).has_attribute("adjacent")
				? gamemap::location::parse_directions((*i)["adjacent"]) : default_dirs;
			std::vector<gamemap::location::DIRECTION>::const_iterator j, j_end = dirs.end();
			for (j = dirs.begin(); j != j_end; ++j) {
				unit_map::const_iterator unit_itor = units_->find(adjacent[*j]);
				if (unit_itor == units_->end()
				|| !unit_itor->second.matches_filter(*i, unit_itor->first, use_flat_tod)) {
					continue;
				}
				if (!(*i).has_attribute("is_enemy")
				|| utils::string_bool((*i)["is_enemy"]) == teams_manager::get_teams()[this->side()-1].is_enemy(unit_itor->second.side())) {
					++match_count;
				}
			}
			static std::vector<std::pair<int,int> > default_counts = utils::parse_ranges("1-6");
			std::vector<std::pair<int,int> > counts = (*i).has_attribute("count")
				? utils::parse_ranges((*i)["count"]) : default_counts;
			if(!in_ranges(match_count, counts)) {
				return false;
			}
		}
	}

	if(cfg.has_attribute("find_in")) {
		// Allow filtering by searching a stored variable of units
		assert(gamestatus_ != NULL);
		variable_info vi(cfg["find_in"], false, variable_info::TYPE_CONTAINER);
		if(!vi.is_valid) return false;
		if(vi.explicit_index) {
			if(id_ != (vi.vars->get_children(vi.key)[vi.index])->get_attribute("id")) {
				return false;
			}
		} else {
			config::child_itors ch_itors = vi.vars->child_range(vi.key);
			for(; ch_itors.first != ch_itors.second; ++ch_itors.first) {
				if(id_ == (*ch_itors.first)->get_attribute("id")) {
					break;
				}
			}
			if(ch_itors.first == ch_itors.second) {
				return false;
			}
		}
	}
	if(cfg.has_attribute("formula")) {
		const unit_callable callable(std::pair<gamemap::location, unit>(loc,*this));
		const game_logic::formula form(cfg["formula"]);
		if(!form.execute(callable).as_bool()) {
			return false;
		}
	}

	return true;
}

void unit::read(const config& cfg, bool use_traits, game_state* state)
{
	if(cfg["type"].empty()) {
		throw game::load_game_failed("Attempt to de-serialize a unit with no 'type' field (probably empty)");
	}
	type_ = cfg["type"];

	cfg_ = cfg;
	side_ = lexical_cast_default<int>(cfg["side"]);
	if(side_ <= 0) {
		side_ = 1;
	}

	validate_side(side_);

	// Prevent un-initialized variables
	hit_points_=1;

	if(cfg["gender"].empty()) {
		gender_ = generate_gender(*type(), utils::string_bool(cfg_["random_gender"], false), state);
	} else {
		gender_ = string_gender(cfg["gender"]);
	}

	variation_ = cfg["variation"];

	id_ = cfg["id"];
	if (id_.empty()) {
		id_ = cfg["type"].c_str();
	}
	name_ = cfg["name"];
	std::string custom_unit_desc = cfg["description"];

	underlying_id_ = lexical_cast_default<size_t>(cfg["underlying_id"],0);
	set_underlying_id();

	role_ = cfg["role"];
	ai_special_ = cfg["ai_special"];
	overlays_ = utils::split(cfg["overlays"]);
	if(overlays_.size() == 1 && overlays_.front() == "") {
		overlays_.clear();
	}
	const config* const variables = cfg.child("variables");
	if(variables != NULL) {
		variables_ = *variables;
		cfg_.remove_child("variables",0);
	} else {
		variables_.clear();
	}

	facing_ = gamemap::location::parse_direction(cfg["facing"]);
	if(facing_ == gamemap::location::NDIRECTIONS) facing_ = gamemap::location::SOUTH_EAST;

	recruits_ = utils::split(cfg["recruits"]);
	if(recruits_.size() == 1 && recruits_.front() == "") {
		recruits_.clear();
	}

	const config* mods = cfg.child("modifications");
	if(mods) {
		modifications_ = *mods;
		cfg_.remove_child("modifications",0);
	}

	advance_to(type(), use_traits, state);
	if(cfg["race"] != "") {
		const race_map::const_iterator race_it = unit_type_data::types().races().find(cfg["race"]);
		if(race_it != unit_type_data::types().races().end()) {
			race_ = &race_it->second;
		} else {
			static const unit_race dummy_race;
			race_ = &dummy_race;
		}
	}
	level_ = lexical_cast_default<int>(cfg["level"], level_);
	if(cfg["undead_variation"] != "") {
		undead_variation_ = cfg["undead_variation"];
	}
	if(cfg["max_attacks"] != "") {
		max_attacks_ = std::max<int>(0,lexical_cast_default<int>(cfg["max_attacks"],1));
	}
	attacks_left_ = std::max<int>(0,lexical_cast_default<int>(cfg["attacks_left"], max_attacks_));

	if(cfg["alpha"] != "") {
		alpha_ = lexical_cast_default<fixed_t>(cfg["alpha"]);
	}
	if(cfg["zoc"] != "") {
		emit_zoc_ = utils::string_bool(cfg["zoc"]);
	}
	if(cfg["flying"] != "") {
		flying_ = utils::string_bool(cfg["flying"]);
	}
	if(custom_unit_desc != "") {
		cfg_["description"] = custom_unit_desc;
	}
	if(cfg["cost"] != "") {
		unit_value_ = lexical_cast_default<int>(cfg["cost"]);
	}

	if(cfg["profile"] != "") {
		cfg_["profile"] = cfg["profile"];
	}
	max_hit_points_ = std::max<int>(1,lexical_cast_default<int>(cfg["max_hitpoints"], max_hit_points_));
	max_movement_ = std::max<int>(0,lexical_cast_default<int>(cfg["max_moves"], max_movement_));
	max_experience_ = std::max<int>(1,lexical_cast_default<int>(cfg["max_experience"], max_experience_));

	std::vector<std::string> temp_advances = utils::split(cfg["advances_to"]);
	if(temp_advances.size() == 1 && temp_advances.front() == "null") {
		advances_to_.clear();
	}else if(temp_advances.size() >= 1 && temp_advances.front() != "") {
		advances_to_ = temp_advances;
	}

	//support for unit formulas and unit-specyfic variables in [ai_vars]
	unit_formula_ = cfg["formula"];

	const config* ai_vars = cfg.child("ai_vars");
	if (ai_vars)
	{
		formula_vars_ = new game_logic::map_formula_callable;

		variant var;
		for(string_map::const_iterator i = ai_vars->values.begin(); i != ai_vars->values.end(); ++i)
		{
 			var.serialize_from_string(i->second);
			formula_vars_->add(i->first, var);
		}
	} else {
		formula_vars_ = game_logic::map_formula_callable_ptr();
	}
	//remove ai_vars from private cfg
	cfg_.clear_children("ai_vars");

	//dont use the unit_type's attacks if this config has its own defined
	config::const_child_itors cfg_range = cfg.child_range("attack");
	if(cfg_range.first != cfg_range.second) {
		attacks_.clear();
		do {
			attacks_.push_back(attack_type(**cfg_range.first));
		} while(++cfg_range.first != cfg_range.second);
	}
	cfg_.clear_children("attack");

	//dont use the unit_type's abilities if this config has its own defined
	cfg_range = cfg.child_range("abilities");
	if(cfg_range.first != cfg_range.second) {
		cfg_.clear_children("abilities");
		config &target = cfg_.add_child("abilities");
		do {
			target.append(**cfg_range.first);
		} while(++cfg_range.first != cfg_range.second);
	}

	//dont use the unit_type's defense if this config has its own defined
	cfg_range = cfg.child_range("defense");
	if(cfg_range.first != cfg_range.second) {
		cfg_.clear_children("defense");
		defense_mods_.clear();
		config &target = cfg_.add_child("defense");
		do {
			target.append(**cfg_range.first);
		} while(++cfg_range.first != cfg_range.second);
	}

	//dont use the unit_type's movement costs if this config has its own defined
	cfg_range = cfg.child_range("movement_costs");
	if(cfg_range.first != cfg_range.second) {
		cfg_.clear_children("movement_costs");
		movement_costs_.clear();
		config &target = cfg_.add_child("movement_costs");
		do {
			target.append(**cfg_range.first);
		} while(++cfg_range.first != cfg_range.second);
	}

	//dont use the unit_type's resistance if this config has its own defined
	cfg_range = cfg.child_range("resistance");
	if(cfg_range.first != cfg_range.second) {
		cfg_.clear_children("resistance");
		config &target = cfg_.add_child("resistance");
		do {
			target.append(**cfg_range.first);
		} while(++cfg_range.first != cfg_range.second);
	}

	const config* status_flags = cfg.child("status");
	if(status_flags) {
		for(string_map::const_iterator st = status_flags->values.begin(); st != status_flags->values.end(); ++st) {
			states_[st->first] = st->second;
		}
		cfg_.remove_child("status",0);
	}
	if(cfg["ai_special"] == "guardian") {
		set_state("guardian","yes");
	}

	// Attach animations for this unit to the in-core object
	unit_animation::fill_initial_animations(animations_,cfg_);
	// Remove animations from private cfg, they're not needed there now
	foreach(const std::string& tag_name, unit_animation::all_tag_names()) {
		cfg_.clear_children(tag_name);
	}

	if(cfg["hitpoints"] != "") {
		hit_points_ = lexical_cast_default<int>(cfg["hitpoints"]);
	} else {
		hit_points_ = max_hit_points_;
	}
	goto_.x = lexical_cast_default<int>(cfg["goto_x"]) - 1;
	goto_.y = lexical_cast_default<int>(cfg["goto_y"]) - 1;
	if(cfg["moves"] != "") {
		movement_ = lexical_cast_default<int>(cfg["moves"]);
		if(movement_ < 0) {
			attacks_left_ = 0;
			movement_ = 0;
		}
	} else {
		movement_ = max_movement_;
	}
	experience_ = lexical_cast_default<int>(cfg["experience"]);
	resting_ = utils::string_bool(cfg["resting"]);
	unrenamable_ = utils::string_bool(cfg["unrenamable"]);
	if(cfg["alignment"]=="lawful") {
		alignment_ = unit_type::LAWFUL;
	} else if(cfg["alignment"]=="neutral") {
		alignment_ = unit_type::NEUTRAL;
	} else if(cfg["alignment"]=="chaotic") {
		alignment_ = unit_type::CHAOTIC;
	} else if(cfg["type"]=="") {
		alignment_ = unit_type::NEUTRAL;
	}
	if(utils::string_bool(cfg["generate_name"])) {
		name_ = generate_name(state ? &(state->rng()) : 0);
		cfg_["generate_name"] = "";
	}

	game_events::add_events(cfg_.get_children("event"),type_);
	// Make the default upkeep "full"
	if(cfg_["upkeep"].empty()) {
		cfg_["upkeep"] = "full";
	}
}
void unit::write(config& cfg) const
{
	// If a location has been saved in the config, keep it
	std::string x = cfg["x"];
	std::string y = cfg["y"];
	cfg.append(cfg_);
	cfg["x"] = x;
	cfg["y"] = y;
	std::map<std::string,unit_type>::const_iterator uti = unit_type_data::types().find(type_id());
	const unit_type* ut = NULL;
	if(uti != unit_type_data::types().end()) {
		ut = &uti->second.get_gender_unit_type(gender_).get_variation(variation_);
	}
	if(ut && cfg["description"] == ut->unit_description()) {
		cfg["description"] = "";
	}

	std::stringstream hp;
	hp << hit_points_;
	cfg["hitpoints"] = hp.str();
	std::stringstream hpm;
	hpm << max_hit_points_;
	cfg["max_hitpoints"] = hpm.str();

	std::stringstream xp;
	xp << experience_;
	cfg["experience"] = xp.str();
	std::stringstream xpm;
	xpm << max_experience_;
	cfg["max_experience"] = xpm.str();

	std::stringstream sd;
	sd << side_;
	cfg["side"] = sd.str();

	cfg["type"] = type_id();

	//support for unit formulas and unit-specyfic variables in [ai_vars]
	if (has_formula())
		cfg["formula"] = unit_formula_;


	if (formula_vars_ && formula_vars_->empty() == false)
	{
		cfg.add_child("ai_vars");
		config* ai_vars = cfg.child("ai_vars");

		std::string str;
		for(game_logic::map_formula_callable::const_iterator i = formula_vars_->begin(); i != formula_vars_->end(); ++i)
		{
			i->second.serialize_to_string(str);
			if (!str.empty())
			{
				(*ai_vars)[i->first] = str;
				str.clear();
			}
		}
	}


	cfg["gender"] = gender_string(gender_);

	cfg["variation"] = variation_;

	cfg["role"] = role_;
	cfg["ai_special"] = ai_special_;
	cfg["flying"] = flying_ ? "yes" : "no";

	config status_flags;
	for(std::map<std::string,std::string>::const_iterator st = states_.begin(); st != states_.end(); ++st) {
		status_flags[st->first] = st->second;
	}

	cfg.clear_children("variables");
	cfg.add_child("variables",variables_);
	cfg.clear_children("status");
	cfg.add_child("status",status_flags);

	cfg["overlays"] = utils::join(overlays_);

	cfg["name"] = name_;
	cfg["id"] = id_;
	cfg["underlying_id"] = lexical_cast<std::string>(underlying_id_);

	if(can_recruit())
		cfg["canrecruit"] = "yes";

	cfg["facing"] = gamemap::location::write_direction(facing_);

	cfg["goto_x"] = lexical_cast_default<std::string>(goto_.x+1);
	cfg["goto_y"] = lexical_cast_default<std::string>(goto_.y+1);

	cfg["moves"] = lexical_cast_default<std::string>(movement_);
	cfg["max_moves"] = lexical_cast_default<std::string>(max_movement_);

	cfg["resting"] = resting_ ? "yes" : "no";

	cfg["advances_to"] = utils::join(advances_to_);

	cfg["race"] = race_->id();
	cfg["language_name"] = type_name_;
	cfg["undead_variation"] = undead_variation_;
	cfg["variation"] = variation_;
	cfg["level"] = lexical_cast_default<std::string>(level_);
	switch(alignment_) {
		case unit_type::LAWFUL:
			cfg["alignment"] = "lawful";
			break;
		case unit_type::NEUTRAL:
			cfg["alignment"] = "neutral";
			break;
		case unit_type::CHAOTIC:
			cfg["alignment"] = "chaotic";
			break;
		default:
			cfg["alignment"] = "neutral";
	}
	cfg["flag_rgb"] = flag_rgb_;
	cfg["unrenamable"] = unrenamable_ ? "yes" : "no";
	cfg["alpha"] = lexical_cast_default<std::string>(alpha_);

	cfg["recruits"] = utils::join(recruits_);
	cfg["attacks_left"] = lexical_cast_default<std::string>(attacks_left_);
	cfg["max_attacks"] = lexical_cast_default<std::string>(max_attacks_);
	cfg["zoc"] = emit_zoc_ ? "yes" : "no";
	cfg.clear_children("attack");
	for(std::vector<attack_type>::const_iterator i = attacks_.begin(); i != attacks_.end(); ++i) {
		cfg.add_child("attack",i->get_cfg());
	}
	cfg["cost"] = lexical_cast_default<std::string>(unit_value_);
	cfg.clear_children("modifications");
	cfg.add_child("modifications",modifications_);

}


const surface unit::still_image(bool scaled) const
{
	image::locator image_loc;

#ifdef LOW_MEM
	image_loc = image::locator(absolute_image());
#else
	std::string mods=image_mods();
	if(mods.size()){
		image_loc = image::locator(absolute_image(),mods);
	} else {
		image_loc = image::locator(absolute_image());
	}
#endif

	surface unit_image(image::get_image(image_loc, scaled ? image::SCALED_TO_ZOOM : image::UNSCALED));
	return unit_image;
}

void unit::set_standing(const gamemap::location& loc, bool with_bars)
{
	game_display * disp =  game_display::get_singleton();
	gamemap::location arr[6];
	get_adjacent_tiles(loc, arr);
	if (preferences::show_standing_animations()&& !incapacitated()) {
		start_animation(INT_MAX,loc,choose_animation(*disp,loc,"standing"),with_bars,true,"",0,STATE_STANDING);
	} else {
		start_animation(INT_MAX,loc,choose_animation(*disp,loc,"_disabled_"),with_bars,true,"",0,STATE_STANDING);
	}
}




void unit::set_idling(const game_display &disp,const gamemap::location& loc)
{
	start_animation(INT_MAX,loc,choose_animation(disp,loc,"idling"),true,false,"",0,STATE_FORGET);
}

void unit::set_selecting(const game_display &disp,const gamemap::location& loc)
{
	if (preferences::show_standing_animations()) {
		start_animation(INT_MAX,loc,choose_animation(disp,loc,"selected"),true,false,"",0,STATE_FORGET);
	} else {
		start_animation(INT_MAX,loc,choose_animation(disp,loc,"_disabled_selected_"),true,false,"",0,STATE_STANDING);
	}
}

void unit::start_animation(const int start_time, const gamemap::location &loc,const unit_animation * animation,bool with_bars,bool cycles,const std::string text, const Uint32 text_color,STATE state)
{
	const game_display * disp =  game_display::get_singleton();
    // everything except standing select and idle
    const bool accelerate = (state != STATE_FORGET && state != STATE_STANDING);
	if(!animation) {
		set_standing(loc,with_bars);
		return ;
	}
	state_ =state;
	draw_bars_ =  with_bars;
	if(anim_) delete anim_;
	anim_ = new unit_animation(*animation);
	const int real_start_time = start_time == INT_MAX ? anim_->get_begin_time() : start_time;
	anim_->start_animation(real_start_time,loc, loc.get_direction(facing_), cycles,text,text_color,accelerate);
	frame_begin_time_ = anim_->get_begin_time() -1;
	if (disp->idle_anim()) {
		next_idling_ = get_current_animation_tick()
			+ static_cast<int>((20000 + rand() % 20000) * disp->idle_anim_rate());
	} else {
		next_idling_ = INT_MAX;
	}
}


void unit::set_facing(gamemap::location::DIRECTION dir) {
	if(dir != gamemap::location::NDIRECTIONS) {
		facing_ = dir;
	}
	// Else look at yourself (not available so continue to face the same direction)
}

void unit::redraw_unit(game_display& disp, const gamemap::location& loc, const bool fake)
{
	const gamemap & map = disp.get_map();
	if(!loc.valid() || hidden_ || disp.fogged(loc)
	|| (invisible(loc,disp.get_units(),disp.get_teams())
	&& disp.get_teams()[disp.viewing_team()].is_enemy(side())))
	{
		clear_haloes();
		if(anim_) {
			anim_->update_last_draw_time();
		}
		return;
	}
	if(refreshing_) {
		return;
	}
	refreshing_ = true;


	if(!anim_) {
		set_standing(loc);
	}
	anim_->update_last_draw_time();
	frame_parameters params;
	const t_translation::t_terrain terrain = map.get_terrain(loc);
	const terrain_type& terrain_info = map.get_terrain_info(terrain);
	// do not set to 0 so we can distinguih the flying from the "not on submerge terrain"
	 params.submerge= is_flying() ? 0.01 : terrain_info.unit_submerge();

	if(invisible(loc,disp.get_units(),disp.get_teams()) &&
			params.highlight_ratio > 0.5) {
		params.highlight_ratio = 0.5;
	}
	if(loc == disp.selected_hex() && params.highlight_ratio == 1.0) {
		params.highlight_ratio = 1.5;
	}
	int height_adjust = static_cast<int>(terrain_info.unit_height_adjust() * disp.get_zoom_factor());
	if (is_flying() && height_adjust < 0) {
		height_adjust = 0;
	}
	params.y -= height_adjust;
	params.halo_y -= height_adjust;
	if (utils::string_bool(get_state("poisoned")) ){
		params.blend_with = disp.rgb(0,255,0);
		params.blend_ratio = 0.25;
	}
	params.image_mod = image_mods();
#ifdef LOW_MEM
	params.image= absolute_image();
	params.image_diagonal= absolute_image();
#endif
	if(utils::string_bool(get_state("stoned"))) params.image_mod +="~GS()";

	const frame_parameters adjusted_params = anim_->get_current_params(params,true);



#ifndef LOW_MEM
	bool facing_west = facing_ == gamemap::location::NORTH_WEST || facing_ == gamemap::location::SOUTH_WEST;
#else
	bool facing_west = false;
#endif
	const gamemap::location dst = loc.get_direction(facing_);
	const int xsrc = disp.get_location_x(loc);
	const int ysrc = disp.get_location_y(loc);
	const int xdst = disp.get_location_x(dst);
	const int ydst = disp.get_location_y(dst);
	const int drawing_order = gamemap::get_drawing_order(loc);


	if(frame_begin_time_ != anim_->get_current_frame_begin_time()) {
		frame_begin_time_ = anim_->get_current_frame_begin_time();
		if(!adjusted_params.sound.empty()) {
			sound::play_sound(adjusted_params.sound);
		}
		if(!adjusted_params.text.empty()  ) {
			game_display::get_singleton()->float_label(loc,adjusted_params.text,
			(adjusted_params.text_color & 0x00FF0000) >> 16,
			(adjusted_params.text_color & 0x0000FF00) >> 8,
			(adjusted_params.text_color & 0x000000FF) >> 0);
		}
	}

	int d2 = disp.hex_size() / 2;
	const int x = static_cast<int>(adjusted_params.offset * xdst + (1.0-adjusted_params.offset) * xsrc) + d2;
	const int y = static_cast<int>(adjusted_params.offset * ydst + (1.0-adjusted_params.offset) * ysrc) + d2;


	if(unit_halo_ == halo::NO_HALO && !image_halo().empty()) {
		unit_halo_ = halo::add(0, 0, image_halo(), gamemap::location(-1, -1));
	}
	if(unit_halo_ != halo::NO_HALO && image_halo().empty()) {
		halo::remove(unit_halo_);
		unit_halo_ = halo::NO_HALO;
	} else if(unit_halo_ != halo::NO_HALO) {
		halo::set_location(unit_halo_, x, y);
	}

	if(unit_anim_halo_ != halo::NO_HALO) {
		halo::remove(unit_anim_halo_);
		unit_anim_halo_ = halo::NO_HALO;
	}
	if(!adjusted_params.halo.empty()) {
		int dx = static_cast<int>(adjusted_params.halo_x * disp.get_zoom_factor());
		int dy = static_cast<int>(adjusted_params.halo_y * disp.get_zoom_factor());
		if (facing_west) dx = -dx;
		unit_anim_halo_ = halo::add(x + dx, y+ dy,
			adjusted_params.halo, gamemap::location(-1, -1),
			facing_west ? halo::HREVERSE : halo::NORMAL);
	}


	image::locator image_loc = image::locator();
	if(facing_ != gamemap::location::NORTH && facing_ != gamemap::location::SOUTH) {
		image_loc = adjusted_params.image_diagonal;
	}
	if(image_loc.is_void()|| image_loc.get_filename() == "") { // invalid diag image, or not diagonal
		image_loc = adjusted_params.image;
	}
	if(image_loc.is_void()|| image_loc.get_filename() == "") {
		image_loc = absolute_image();
	}
	image_loc = image::locator(image_loc,adjusted_params.image_mod);

	surface image(image::get_image(image_loc, image::SCALED_TO_ZOOM));
	if(image == NULL) {
		image = still_image(true);
	}




	// We draw bars only if wanted, visible on the map view and not a fake unit
	bool draw_bars = draw_bars_ && !fake;
	if (draw_bars) {
		const int d = disp.hex_size();
		SDL_Rect unit_rect = {xsrc, ysrc +adjusted_params.y, d, d};
		draw_bars = rects_overlap(unit_rect, disp.map_outside_area());
	}

	surface ellipse_front(NULL);
	surface ellipse_back(NULL);
	int ellipse_floating = 0;
	if(draw_bars && preferences::show_side_colours()) {
		// The division by 2 seems to have no real meaning,
		// It just works fine with the current center of ellipse
		// and prevent a too large adjust if submerge = 1.0
		ellipse_floating = static_cast<int>(adjusted_params.submerge * disp.hex_size() / 2);

		std::string ellipse=image_ellipse();
		if(ellipse.empty()){
			ellipse="misc/ellipse";
		}

		const char* const selected = disp.selected_hex() == loc ? "selected-" : "";

		// Load the ellipse parts recolored to match team color
		char buf[100];
		std::string tc=team::get_side_colour_index(side_);

		snprintf(buf,sizeof(buf),"%s-%stop.png~RC(ellipse_red>%s)",ellipse.c_str(),selected,tc.c_str());
		ellipse_back.assign(image::get_image(image::locator(buf), image::SCALED_TO_ZOOM));
		snprintf(buf,sizeof(buf),"%s-%sbottom.png~RC(ellipse_red>%s)",ellipse.c_str(),selected,tc.c_str());
		ellipse_front.assign(image::get_image(image::locator(buf), image::SCALED_TO_ZOOM));
	}

	// FIXME: Use the hack to draw ellipses in the unit layer
	// but with a different drawing_order, so it's rendered behind/above unit

	if (ellipse_back != NULL) {
		//disp.drawing_buffer_add(display::LAYER_UNIT_BG, drawing_order,
		disp.drawing_buffer_add(display::LAYER_UNIT_FIRST, drawing_order-10,
			display::tblit(xsrc, ysrc +adjusted_params.y-ellipse_floating, ellipse_back));
	}

	if (image != NULL) {
		const int tmp_x = adjusted_params.x +x - image->w/2;
		const int tmp_y = adjusted_params.y +y - image->h/2;
		disp.render_unit_image(tmp_x, tmp_y, fake, drawing_order, image, facing_west, false,
				ftofxp(adjusted_params.highlight_ratio), adjusted_params.blend_with, adjusted_params.blend_ratio, adjusted_params.submerge);
	}

	if (ellipse_front != NULL) {
		//disp.drawing_buffer_add(display::LAYER_UNIT_FG, drawing_order,
		disp.drawing_buffer_add(display::LAYER_UNIT_FIRST, drawing_order+10,
			display::tblit(xsrc, ysrc +adjusted_params.y-ellipse_floating, ellipse_front));
	}

	if(draw_bars) {
		const std::string* movement_file = NULL;
		const std::string* energy_file = &game_config::energy_image;

		if(size_t(side()) != disp.viewing_team()+1) {
			if(disp.team_valid() &&
			   disp.get_teams()[disp.viewing_team()].is_enemy(side())) {
				movement_file = &game_config::enemy_ball_image;
			} else {
				movement_file = &game_config::ally_ball_image;
			}
		} else {
			movement_file = &game_config::moved_ball_image;
			if(disp.playing_team() == disp.viewing_team() && !user_end_turn()) {
				if (movement_left() == total_movement()) {
					movement_file = &game_config::unmoved_ball_image;
					// unit_can_move assumes that it's not a fake unit (= in unit_map)
				} else if(unit_can_move(loc,disp.get_units(),map,disp.get_teams())) {
					movement_file = &game_config::partmoved_ball_image;
				}
			}
		}

		surface orb(image::get_image(*movement_file,image::SCALED_TO_ZOOM));
		if (orb != NULL) {
			disp.drawing_buffer_add(display::LAYER_UNIT_BAR,
				drawing_order, display::tblit(xsrc, ysrc +adjusted_params.y, orb));
		}

		double unit_energy = 0.0;
		if(max_hitpoints() > 0) {
			unit_energy = double(hitpoints())/double(max_hitpoints());
		}
#ifdef USE_TINY_GUI
		const int bar_shift = static_cast<int>(-2.5*disp.get_zoom_factor());
#else
		const int bar_shift = static_cast<int>(-5*disp.get_zoom_factor());
#endif
		const int hp_bar_height = static_cast<int>(max_hitpoints()*game_config::hp_bar_scaling);

		const fixed_t bar_alpha = (loc == disp.mouseover_hex() || loc == disp.selected_hex()) ? ftofxp(1.0): ftofxp(0.8);

		disp.draw_bar(*energy_file, xsrc+bar_shift, ysrc +adjusted_params.y,
			drawing_order, hp_bar_height, unit_energy,hp_color(), bar_alpha);

		if(experience() > 0 && can_advance()) {
			const double filled = double(experience())/double(max_experience());

			const int xp_bar_height = static_cast<int>(max_experience()*game_config::xp_bar_scaling / std::max<int>(level_,1));

			SDL_Color colour=xp_color();
			disp.draw_bar(*energy_file, xsrc, ysrc +adjusted_params.y,
				drawing_order, xp_bar_height, filled, colour, bar_alpha);
		}

		if (can_recruit()) {
			surface crown(image::get_image("misc/leader-crown.png",image::SCALED_TO_ZOOM));
			if(!crown.null()) {
				//if(bar_alpha != ftofxp(1.0)) {
				//	crown = adjust_surface_alpha(crown, bar_alpha);
				//}
				disp.drawing_buffer_add(display::LAYER_UNIT_BAR,
					drawing_order, display::tblit(xsrc, ysrc +adjusted_params.y, crown));
			}
		}

		for(std::vector<std::string>::const_iterator ov = overlays().begin(); ov != overlays().end(); ++ov) {
			const surface ov_img(image::get_image(*ov, image::SCALED_TO_ZOOM));
			if(ov_img != NULL) {
				disp.drawing_buffer_add(display::LAYER_UNIT_BAR,
					drawing_order, display::tblit(xsrc, ysrc +adjusted_params.y, ov_img));
			}
		}
	}

	anim_->redraw(params);
	refreshing_ = false;
}

void unit::clear_haloes()
{
	if(unit_halo_ != halo::NO_HALO) {
		halo::remove(unit_halo_);
		unit_halo_ = halo::NO_HALO;
	}
	if(unit_anim_halo_ != halo::NO_HALO) {
		halo::remove(unit_anim_halo_);
		unit_anim_halo_ = halo::NO_HALO;
	}
}
bool unit::invalidate(const gamemap::location &loc)
{
	bool result = false;

	// Very early calls, anim not initialized yet
	if(get_animation()) {
		frame_parameters params;
		game_display * disp =  game_display::get_singleton();
		const gamemap & map = disp->get_map();
		const t_translation::t_terrain terrain = map.get_terrain(loc);
		const terrain_type& terrain_info = map.get_terrain_info(terrain);
		if(!params.submerge) params.submerge=	is_flying() ? 0.0 : terrain_info.unit_submerge();

		if(invisible(loc,disp->get_units(),disp->get_teams()) &&
				params.highlight_ratio > 0.5) {
			params.highlight_ratio = 0.5;
		}
		if(loc == disp->selected_hex() && params.highlight_ratio == 1.0) {
			params.highlight_ratio = 1.5;
		}
		int height_adjust = static_cast<int>(terrain_info.unit_height_adjust() * disp->get_zoom_factor());
		if (is_flying() && height_adjust < 0) {
			height_adjust = 0;
		}
		params.y -= height_adjust;
		params.halo_y -= height_adjust;
		if (utils::string_bool(get_state("poisoned")) ){
			params.blend_with = disp->rgb(0,255,0);
			params.blend_ratio = 0.25;
		}
		params.image_mod = image_mods();

                //get_animation()->update_last_draw_time();
		frame_parameters adjusted_params= get_animation()->get_current_params(params);
		result |= get_animation()->invalidate(params);
	}


	if (abilities_affects_adjacent())
	{
		gamemap::location arr[6];
		get_adjacent_tiles(loc, arr);
		for (unsigned int i = 0; i < 6; i++) {
			result |= game_display::get_singleton()->invalidate(arr[i]);
		}
	}
	return result;

}

int unit::upkeep() const
{
	// Leaders do not incur upkeep.
	if(can_recruit()) {
		return 0;
	}
	if(cfg_["upkeep"] == "full") {
		return level();
	}
	if(cfg_["upkeep"] == "loyal") {
		return 0;
	}
	return lexical_cast_default<int>(cfg_["upkeep"]);
}

int unit::movement_cost_internal(const t_translation::t_terrain terrain, const int recurse_count) const
{
	const int impassable = 10000000;

	const std::map<t_translation::t_terrain,int>::const_iterator i =
	movement_costs_.find(terrain);

	if(i != movement_costs_.end()) {
		return i->second;
	}

	assert(map_ != NULL);
	// If this is an alias, then select the best of all underlying terrains
	const t_translation::t_list& underlying = map_->underlying_mvt_terrain(terrain);

	assert(!underlying.empty());
	if(underlying.size() != 1 || underlying.front() != terrain) { // We fail here, but first test underlying_mvt_terrain
		bool revert = (underlying.front() == t_translation::MINUS ? true : false);
		if(recurse_count >= 100) {
			return impassable;
		}

		int ret_value = revert ? 0: impassable;
		for(t_translation::t_list::const_iterator i = underlying.begin();
		i != underlying.end(); ++i) {
			if(*i == t_translation::PLUS) {
				revert = false;
				continue;
			} else if(*i == t_translation::MINUS) {
				revert = true;
				continue;
			}
			const int value = movement_cost_internal(*i,recurse_count+1);
			if(value < ret_value && !revert) {
				ret_value = value;
			} else if(value > ret_value && revert) {
				ret_value = value;
			}
		}

		movement_costs_.insert(std::pair<t_translation::t_terrain, int>(terrain, ret_value));
		return ret_value;
	}

	const config* movement_costs = cfg_.child("movement_costs");

	int res = -1;
	if(movement_costs != NULL) {
		if(underlying.size() != 1) {
			LOG_STREAM(err, config) << "terrain '" << terrain << "' has "
				<< underlying.size() << " underlying names - 0 expected\n";
			return impassable;
		}
		const std::string& id = map_->get_terrain_info(underlying.front()).id();
		const std::string& val = (*movement_costs)[id];
		if(val != "") {
			res = atoi(val.c_str());
		}
	}

	if(res <= 0) {
		res = impassable;
	}

	movement_costs_.insert(std::pair<t_translation::t_terrain, int>(terrain,res));
	return res;
}

int unit::movement_cost(const t_translation::t_terrain terrain) const
{
	const int res = movement_cost_internal(terrain, 0);
	if(utils::string_bool(get_state("slowed"))) {
		return res*2;
	}
	return res;
}

int unit::defense_modifier(t_translation::t_terrain terrain, int recurse_count) const
{
	const std::map<t_translation::t_terrain,int>::const_iterator d = defense_mods_.find(terrain);
	if(d != defense_mods_.end()) {
		return d->second;
	}

	assert(map_ != NULL);
	// If this is an alias, then select the best of all underlying terrains
	const t_translation::t_list& underlying = map_->underlying_def_terrain(terrain);
	assert(underlying.size() > 0);
	if(underlying.size() != 1 || underlying.front() != terrain) {
		bool revert = (underlying.front() == t_translation::MINUS ? true : false);
		if(recurse_count >= 90) {
			LOG_STREAM(err, config) << "infinite defense_modifier recursion: " << t_translation::write_terrain_code(terrain) << " depth " << recurse_count << "\n";
		}
		if(recurse_count >= 100) {
			return 100;
		}

		int ret_value = revert?0:100;
		t_translation::t_list::const_iterator i = underlying.begin();
		for(; i != underlying.end(); ++i) {
			if(*i == t_translation::PLUS) {
				revert = false;
				continue;
			} else if(*i == t_translation::MINUS) {
				revert = true;
				continue;
			}
			const int value = defense_modifier(*i,recurse_count+1);
			if(value < ret_value && !revert) {
				ret_value = value;
			} else if(value > ret_value && revert) {
				ret_value = value;
			}
		}

		defense_mods_.insert(std::pair<t_translation::t_terrain,int>(terrain,ret_value));
		return ret_value;
	}

	int res = -1;

	const config* const defense = cfg_.child("defense");

	if(defense != NULL) {
		if(underlying.size() != 1) {
			ERR_CONFIG << "terrain '" << terrain << "' has "
				<< underlying.size() << " underlying names - 0 expected\n";
			return 100;
		}

		const std::string& id = map_->get_terrain_info(underlying.front()).id();
		const std::string& val = (*defense)[id];
		if(val != "") {
			res = atoi(val.c_str());
		}
	}
	if(res < 0) {
		ERR_CONFIG << "Defence '" << res << "' is '< 0' reset to 0 (100% defence).\n";
		res = 0;
	}

	defense_mods_.insert(std::pair<t_translation::t_terrain,int>(terrain,res));
	return res;
}

bool unit::resistance_filter_matches(const config& cfg,bool attacker,const std::string& damage_name) const
{
	if(!(cfg["active_on"]=="" || (attacker && cfg["active_on"]=="offense") || (!attacker && cfg["active_on"]=="defense"))) {
		return false;
	}
	const std::string& apply_to = cfg["apply_to"];
	if(!apply_to.empty()) {
		if(damage_name != apply_to) {
			if(std::find(apply_to.begin(),apply_to.end(),',') != apply_to.end() &&
				std::search(apply_to.begin(),apply_to.end(),
				damage_name.begin(),damage_name.end()) != apply_to.end()) {
				const std::vector<std::string>& vals = utils::split(apply_to);
				if(std::find(vals.begin(),vals.end(),damage_name) == vals.end()) {
					return false;
				}
			} else {
				return false;
			}
		}
	}
	return true;
}


int unit::resistance_against(const std::string& damage_name,bool attacker,const gamemap::location& loc) const
{
	int res = 0;

	const config* const resistance = cfg_.child("resistance");
	if(resistance != NULL) {
		const std::string& val = (*resistance)[damage_name];
		if(val != "") {
			res = 100 - lexical_cast_default<int>(val);
		}
	}

	unit_ability_list resistance_abilities = get_abilities("resistance",loc);
	for(std::vector<std::pair<config*,gamemap::location> >::iterator i = resistance_abilities.cfgs.begin(); i != resistance_abilities.cfgs.end();) {
		if(!resistance_filter_matches(*i->first,attacker,damage_name)) {
			i = resistance_abilities.cfgs.erase(i);
		} else {
			++i;
		}
	}
	if(!resistance_abilities.empty()) {
		unit_abilities::effect resist_effect(resistance_abilities,res,false);

		res = std::min<int>(resist_effect.get_composite_value(),resistance_abilities.highest("max_value").first);
	}
	return 100 - res;
}

string_map unit::get_base_resistances() const
{
	const config* const resistance = cfg_.child("resistance");
	if(resistance != NULL) {
		return resistance->values;
	}
	return string_map();
}

#if 0
std::map<terrain_type::TERRAIN,int> unit::movement_type() const
{
	return movement_costs_;
}
#endif

std::map<std::string,std::string> unit::advancement_icons() const
{
	std::map<std::string,std::string> temp;
	std::string image;
	if(can_advance()){
		if(advances_to_.empty()==false){
			std::stringstream tooltip;
			image=game_config::level_image;
			std::vector<std::string> adv=advances_to();
			for(std::vector<std::string>::const_iterator i=adv.begin();i!=adv.end();i++){
				if((*i).size()){
					tooltip<<(*i).c_str()<<"\n";
				}
			}
			temp[image]=tooltip.str();
		}
		const config::child_list mods=get_modification_advances();
		for(config::child_list::const_iterator i = mods.begin(); i != mods.end(); ++i) {
			image=(**i)["image"];
			if(image.size()){
				std::stringstream tooltip;
				tooltip<<temp[image];
				std::string tt=(**i)["description"];
				if(tt.size()){
					tooltip<<tt<<"\n";
				}
				temp[image]=tooltip.str();
			}
		}
	}
	return(temp);
}
std::vector<std::pair<std::string,std::string> > unit::amla_icons() const
{
	std::vector<std::pair<std::string,std::string> > temp;
	std::pair<std::string,std::string> icon; //<image,tooltip>

	const config::child_list& advances = get_modification_advances();
	for(config::child_list::const_iterator i = advances.begin(); i != advances.end(); ++i) {
		icon.first=(**i)["icon"];
		icon.second=(**i)["description"];

		for(unsigned int j=0;j<(modification_count("advance",(**i)["id"]));j++) {

			temp.push_back(icon);
		}
	}
	return(temp);
}

void unit::reset_modifications()
{
	const std::string mod_childs[] = {"attacks","movement_costs","defense","resistance","abilities"};
	const unit_type *t = type();
	if(t == NULL) {
		return;
	}

	// Reset the scalar values first
	traits_description_ = "";
	is_fearless_ = false;
	is_healthy_ = false;
	max_hit_points_ = t->hitpoints();
	max_experience_ = t->experience_needed(false);
	max_movement_ = t->movement();
	attacks_ = t->attacks();

	// Clear modification-related caches
	modification_descriptions_.clear();
	movement_costs_.clear();
	defense_mods_.clear();

	// Clear modified configs
	foreach(const std::string& tag, mod_childs) {
		cfg_.clear_children(tag);
	}

	// Restore unmodified configs
	if(t->movement_type().get_parent()) {
		//before merging the base movementtype, first get the parent movetype
		//(...this doesn't look right but it was copied from advance_to()...)
		cfg_.merge_with(t->movement_type().get_parent()->get_cfg());
	}
	config to_merge;
	foreach(const std::string& tag, mod_childs) {
		foreach(config* child, t->cfg_.get_children(tag)) {
			to_merge.add_child(tag, *child);
		}
	}
	cfg_.merge_with(to_merge);
}

config::child_list unit::get_modification_advances() const
{
	config::child_list res;
	const config::child_list& advances = modification_advancements();
	for(config::child_list::const_iterator i = advances.begin(); i != advances.end(); ++i) {
		if (!utils::string_bool((**i)["strict_amla"]) || advances_to_.empty()) {
			if(modification_count("advance",(**i)["id"]) < lexical_cast_default<size_t>((**i)["max_times"],1)) {
				std::vector<std::string> temp = utils::split((**i)["require_amla"]);
				if(temp.size()){
					std::sort(temp.begin(),temp.end());
					std::vector<std::string> uniq;
					std::unique_copy(temp.begin(),temp.end(),std::back_inserter(uniq));
					bool requirements_done=true;
					for(std::vector<std::string>::const_iterator ii = uniq.begin(); ii != uniq.end(); ii++){
						int required_num = std::count(temp.begin(),temp.end(),*ii);
						int mod_num = modification_count("advance",*ii);
						if(required_num>mod_num){
							requirements_done=false;
						}
					}
					if(requirements_done){
						res.push_back(*i);
					}
				}else{
					res.push_back(*i);
				}
			}
		}
	}

	return res;
}

size_t unit::modification_count(const std::string& type, const std::string& id) const
{
	size_t res = 0;
	const config::child_list& items = modifications_.get_children(type);
	for(config::child_list::const_iterator i = items.begin(); i != items.end(); ++i) {
		if((**i)["id"] == id) {
			++res;
		}
	}

	return res;
}

/** Helper function for add_modifications */
static void mod_mdr_merge(config& dst, const config& mod, bool delta)
{
	string_map::const_iterator iter = mod.values.begin();
	string_map::const_iterator end = mod.values.end();
	for (; iter != end; iter++) {
		dst[iter->first] =
			lexical_cast_default<std::string>(
				(delta == true)*lexical_cast_default<int>(dst[iter->first])
				+ lexical_cast_default<int>(iter->second)
			);
	}
}

void unit::add_modification(const std::string& type, const config& mod, bool no_add)
{
	if(no_add == false) {
		modifications_.add_child(type,mod);
	}

	std::vector<t_string> effects_description;
	for(config::const_child_itors i = mod.child_range("effect");
		i.first != i.second; ++i.first) {

		// See if the effect only applies to certain unit types
		const std::string& type_filter = (**i.first)["unit_type"];
		if(type_filter.empty() == false) {
			const std::vector<std::string>& types = utils::split(type_filter);
			if(std::find(types.begin(),types.end(),type_id()) == types.end()) {
				continue;
			}
		}
		// See if the effect only applies to certain genders
		const std::string& gender_filter = (**i.first)["unit_gender"];
		if(gender_filter.empty() == false) {
			const std::string& gender = gender_string(gender_);
			const std::vector<std::string>& genders = utils::split(gender_filter);
			if(std::find(genders.begin(),genders.end(),gender) == genders.end()) {
				continue;
			}
		}

		const std::string& apply_to = (**i.first)["apply_to"];
		const std::string& apply_times = (**i.first)["times"];
		int times = 1;
		t_string description;

		if (apply_times == "per level")
			times = level_;
		if (times) {
			while (times > 0) {
				times --;

				// Apply variations -- only apply if we are adding this
				// for the first time.
				if(apply_to == "variation" && no_add == false) {
					variation_ = (**i.first)["name"];
					advance_to(this->type());
				} else if(apply_to == "type" && no_add == false) {
					type_ = (**i.first)["name"];
					int hit_points = hit_points_;
					int experience = experience_;
					int movement = movement_;
					advance_to(this->type());
					hit_points_ = hit_points;
					experience_ = experience;
					movement_ = movement;
				} else if(apply_to == "profile") {
					const std::string& portrait = (**i.first)["portrait"];
					const std::string& description = (**i.first)["description"];
					if(!portrait.empty()) cfg_["profile"] = portrait;
					if(!description.empty()) cfg_["description"] = description;
					//help::unit_topic_generator(*this, (**i.first)["help_topic"]);
				} else if(apply_to == "new_attack") {
					attacks_.push_back(attack_type(**i.first));
				} else if(apply_to == "remove_attacks") {
					for(std::vector<attack_type>::iterator a = attacks_.begin(); a != attacks_.end(); ++a) {
						if (a->matches_filter(**i.first,false)) {
							attacks_.erase(a--);
						}
					}
				} else if(apply_to == "attack") {

					bool first_attack = true;

					std::string attack_names;
					std::string desc;
					for(std::vector<attack_type>::iterator a = attacks_.begin();
						a != attacks_.end(); ++a) {
						bool affected = a->apply_modification(**i.first,&desc);
						if(affected && desc != "") {
							if(first_attack) {
								first_attack = false;
							} else {
								if (!times)
									attack_names += t_string(N_(" and "), "wesnoth");
							}

							if (!times)
								attack_names += t_string(a->name(), "wesnoth");
						}
					}
					if (attack_names.empty() == false) {
						utils::string_map symbols;
						symbols["attack_list"] = attack_names;
						symbols["effect_description"] = desc;
						description += vgettext("$attack_list|: $effect_description", symbols);
					}
				} else if(apply_to == "hitpoints") {
					LOG_UT << "applying hitpoint mod..." << hit_points_ << "/" << max_hit_points_ << "\n";
					const std::string& increase_hp = (**i.first)["increase"];
					const std::string& heal_full = (**i.first)["heal_full"];
					const std::string& increase_total = (**i.first)["increase_total"];
					const std::string& set_hp = (**i.first)["set"];
					const std::string& set_total = (**i.first)["set_total"];

					// If the hitpoints are allowed to end up greater than max hitpoints
					const std::string& violate_max = (**i.first)["violate_maximum"];

					if(set_hp.empty() == false) {
						if(set_hp[set_hp.size()-1] == '%') {
							hit_points_ = lexical_cast_default<int>(set_hp)*max_hit_points_/100;
						} else {
							hit_points_ = lexical_cast_default<int>(set_hp);
						}
					}
					if(set_total.empty() == false) {
						if(set_total[set_total.size()-1] == '%') {
							max_hit_points_ = lexical_cast_default<int>(set_total)*max_hit_points_/100;
						} else {
							max_hit_points_ = lexical_cast_default<int>(set_total);
						}
					}

					if(increase_total.empty() == false) {
						if (!times)
							description += (increase_total[0] != '-' ? "+" : "") +
								increase_total + " " +
								t_string(N_("HP"), "wesnoth");

						// A percentage on the end means increase by that many percent
						max_hit_points_ = utils::apply_modifier(max_hit_points_, increase_total);
					}

					if(max_hit_points_ < 1)
						max_hit_points_ = 1;

					if(heal_full.empty() == false && utils::string_bool(heal_full,true)) {
						heal_all();
					}

					if(increase_hp.empty() == false) {
						hit_points_ = utils::apply_modifier(hit_points_, increase_hp);
					}

					LOG_UT << "modded to " << hit_points_ << "/" << max_hit_points_ << "\n";
					if(hit_points_ > max_hit_points_ && violate_max.empty()) {
						LOG_UT << "resetting hp to max\n";
						hit_points_ = max_hit_points_;
					}

					if(hit_points_ < 1)
						hit_points_ = 1;
				} else if(apply_to == "movement") {
					const std::string& increase = (**i.first)["increase"];
					const std::string& set_to = (**i.first)["set"];

					if(increase.empty() == false) {
						if (!times)
							description += (increase[0] != '-' ? "+" : "") +
								increase + " " +
								t_string(N_("moves"), "wesnoth");

						max_movement_ = utils::apply_modifier(max_movement_, increase, 1);
					}

					if(set_to.empty() == false) {
						max_movement_ = atoi(set_to.c_str());
					}

					if(movement_ > max_movement_)
						movement_ = max_movement_;
				} else if(apply_to == "max_experience") {
					const std::string& increase = (**i.first)["increase"];

					if(increase.empty() == false) {
						if (!times)
							description += (increase[0] != '-' ? "+" : "") +
								increase + " " +
								t_string(N_("XP to advance"), "wesnoth");

						max_experience_ = utils::apply_modifier(max_experience_, increase, 1);
					}

				} else if(apply_to == "loyal") {
					cfg_["upkeep"] = "loyal";
				} else if(apply_to == "status") {
					const std::string& add = (**i.first)["add"];
					const std::string& remove = (**i.first)["remove"];

					if(add.empty() == false) {
						set_state(add,"yes");
					}

					if(remove.empty() == false) {
						set_state(remove,"");
					}
				} else if (apply_to == "movement_costs") {
					config *mv = cfg_.child("movement_costs");
					config *ap = (**i.first).child("movement_costs");
					const std::string& replace = (**i.first)["replace"];
					if(!mv) {
						mv = &cfg_.add_child("movement_costs");
					}
					if (ap) {
						mod_mdr_merge(*mv, *ap, !utils::string_bool(replace));
					}
					movement_costs_.clear();
				} else if (apply_to == "defense") {
					config *mv = cfg_.child("defense");
					config *ap = (**i.first).child("defense");
					const std::string& replace = (**i.first)["replace"];
					if(!mv) {
						mv = &cfg_.add_child("defense");
					}
					if (ap) {
						mod_mdr_merge(*mv, *ap, !utils::string_bool(replace));
					}
					defense_mods_.clear();
				} else if (apply_to == "resistance") {
					config *mv = cfg_.child("resistance");
					config *ap = (**i.first).child("resistance");
					const std::string& replace = (**i.first)["replace"];
					if(!mv) {
						mv = &cfg_.add_child("resistance");
					}
					if (ap) {
						mod_mdr_merge(*mv, *ap, !utils::string_bool(replace));
					}
				} else if (apply_to == "zoc") {
					const std::string& zoc_value = (**i.first)["value"];
					if(!zoc_value.empty()) {
						emit_zoc_ = utils::string_bool(zoc_value);
					}
				} else if (apply_to == "new_ability") {
					config *ab_effect;
					config *ab = cfg_.child("abilities");
					if(!ab) {
						ab = &cfg_.add_child("abilities");
					}
					ab_effect = (**i.first).child("abilities");
					if (ab_effect) {
						config to_append;
						config::all_children_iterator j, j_end = ab_effect->ordered_end();
						for(j = ab_effect->ordered_begin(); j != j_end; ++j) {
							if(!has_ability_by_id(j.get_child()["id"])) {
								to_append.add_child(j.get_key(), j.get_child());
							}
						}
						ab->append(to_append);
					}
				} else if (apply_to == "remove_ability") {
					config *ab_effect = (**i.first).child("abilities");
					if (ab_effect) {
						config::all_children_iterator j, j_end = ab_effect->ordered_end();
						for(j = ab_effect->ordered_begin(); j != j_end; ++j) {
							remove_ability_by_id(j.get_child()["id"]);
						}
					}
				} else if (apply_to == "image_mod") {
					LOG_UT << "applying image_mod \n";
					std::string mod = (**i.first)["replace"];
					if (!mod.empty()){
						image_mods_ = mod;
					}
					LOG_UT << "applying image_mod \n";
					mod = (**i.first)["add"];
					if (!mod.empty()){
						image_mods_ += mod;
					}

					game_config::add_color_info(**i.first);
					LOG_UT << "applying image_mod \n";
				} else if (apply_to == "new_animation") {
					unit_animation::add_anims(animations_,**i.first);
				}
			} // end while
		} else { // for times = per level & level = 0 we still need to rebuild the descriptions
			if(apply_to == "attack") {

				bool first_attack = true;

				for(std::vector<attack_type>::iterator a = attacks_.begin();
					a != attacks_.end(); ++a) {
					std::string desc;
					bool affected = a->describe_modification(**i.first,&desc);
					if(affected && desc != "") {
						if(first_attack) {
							first_attack = false;
						} else {
							description += t_string(N_(" and "), "wesnoth");
						}

						description += t_string(a->name(), "wesnoth") + ": " + desc;
					}
				}
			} else if(apply_to == "hitpoints") {
				const std::string& increase_total = (**i.first)["increase_total"];

				if(increase_total.empty() == false) {
					description += (increase_total[0] != '-' ? "+" : "") +
						increase_total + " " +
						t_string(N_("HP"), "wesnoth");
				}
			} else if(apply_to == "movement") {
				const std::string& increase = (**i.first)["increase"];

				if(increase.empty() == false) {
					description += (increase[0] != '-' ? "+" : "") +
						increase + t_string(N_(" move"), "wesnoth");
				}
			} else if(apply_to == "max_experience") {
				const std::string& increase = (**i.first)["increase"];

				if(increase.empty() == false) {
					description += (increase[0] != '-' ? "+" : "") +
						increase + " " +
						t_string(N_("XP to advance"), "wesnoth");
				}
			}
		}

		if (apply_times == "per level" && !times) {
			utils::string_map symbols;
			symbols["effect_description"] = description;
			description = vgettext("$effect_description per level", symbols);
		}
		if(!description.empty())
			effects_description.push_back(description);

	}

	t_string& description = modification_descriptions_[type];
	t_string trait_description;

	// Punctuation should be translatable: not all languages use latin punctuation.
	// (However, there maybe is a better way to do it)
	if (!mod["description"].empty()) {
		trait_description += mod["description"] + " ";
	}
	if(effects_description.empty() == false) {
		//trait_description += t_string(N_("("), "wesnoth");
		for(std::vector<t_string>::const_iterator i = effects_description.begin();
				i != effects_description.end(); ++i) {
			trait_description += *i;
			if(i+1 != effects_description.end())
				trait_description += t_string(N_(" and "), "wesnoth");
		}
		//trait_description += t_string(N_(")"), "wesnoth");
	}

	if (!mod["name"].empty()) {
		utils::string_map symbols;
		symbols["trait_name"] = mod["name"];
		symbols["trait_description"] = trait_description;
		description += vgettext("$trait_name|: $trait_description ", symbols);
	} else if (!trait_description.empty()) {
		description += trait_description;
	}

	description += "\n";
}

const t_string& unit::modification_description(const std::string& type) const
{
	const string_map::const_iterator i = modification_descriptions_.find(type);
	if(i == modification_descriptions_.end()) {
		static const t_string empty_string;
		return empty_string;
	} else {
		return i->second;
	}
}



const unit_animation* unit::choose_animation(const game_display& disp, const gamemap::location& loc,const std::string& event,const int value,const unit_animation::hit_type hit,const attack_type* attack,const attack_type* second_attack, int swing_num) const
{
	// Select one of the matching animations at random
	std::vector<const unit_animation*> options;
	int max_val = unit_animation::MATCH_FAIL;
	for(std::vector<unit_animation>::const_iterator i = animations_.begin(); i != animations_.end(); ++i) {
		int matching = i->matches(disp,loc,this,event,value,hit,attack,second_attack,swing_num);
		if(matching > unit_animation::MATCH_FAIL && matching == max_val) {
			options.push_back(&*i);
		} else if(matching > max_val) {
			max_val = matching;
			options.erase(options.begin(),options.end());
			options.push_back(&*i);
		}
	}

	if(max_val == unit_animation::MATCH_FAIL) {
		return NULL;
	}
	return options[rand()%options.size()];
}


void unit::apply_modifications()
{
	log_scope("apply mods");
	std::vector< t_string > traits;
	config::child_list const &mods = modifications_.get_children("trait");
	for(config::child_list::const_iterator j = mods.begin(), j_end = mods.end(); j != j_end; ++j) {
		is_fearless_ = is_fearless_ || (**j)["id"] == "fearless";
		is_healthy_ = is_healthy_ || (**j)["id"] == "healthy";
		const std::string gender_string = gender_ == unit_race::FEMALE ? "female_name" : "male_name";
		t_string const &gender_specific_name = (**j)[gender_string];
		if (!gender_specific_name.empty()) {
			traits.push_back(gender_specific_name);
			(**j)["name"] = gender_specific_name;
		} else {
			t_string const &name = (**j)["name"];
			if (!name.empty()) {
				traits.push_back(name);
			}
		}
	}

	for(size_t i = 0; i != NumModificationTypes; ++i) {
		const std::string& mod = ModificationTypes[i];
		const config::child_list& mods = modifications_.get_children(mod);
		for(config::child_list::const_iterator j = mods.begin(); j != mods.end(); ++j) {
			log_scope("add mod");
			add_modification(ModificationTypes[i],**j,true);
		}
	}

	std::vector< t_string >::iterator k = traits.begin(), k_end = traits.end();
	if (k != k_end) {
		// We want to make sure the traits always have a consistent ordering.
			// Try out not sorting, since quick,resilient can give different HP
			// to resilient,quick so rather preserve order
		// std::sort(k, k_end);
		for(;;) {
			traits_description_ += *(k++);
			if (k == k_end) break;
			traits_description_ += ", ";
		}
	}

	//apply the experience acceleration last
	int exp_accel = unit_type::experience_accelerator::get_acceleration();
	max_experience_ = std::max<int>(1, (max_experience_ * exp_accel + 50)/100);
}

void unit::ambush() const
{
	clear_visibility_cache();
	std::vector<const unit *>::iterator itor =
	std::find(units_with_cache.begin(), units_with_cache.end(), this);

	if(itor != units_with_cache.end()) {
		units_with_cache.erase(itor);
	}
}

bool unit::invisible(const gamemap::location& loc,
		const unit_map& units,const std::vector<team>& teams, bool see_all) const
{
	// Fetch from cache
	/**
	 * @todo FIXME: We use the cache only when using the default see_all=true
	 * Maybe add a second cache if the see_all=false become more frequent.
	 */
	if(see_all) {
		std::map<gamemap::location, bool>::const_iterator itor = invisibility_cache_.find(loc);
		if(itor != invisibility_cache_.end()) {
			return itor->second;
		}
	}

	// Test hidden status
	static const std::string hides("hides");
	bool is_inv = (utils::string_bool(get_state("hidden")) && get_ability_bool(hides,loc));
	if(is_inv){
		for(unit_map::const_iterator u = units.begin(); u != units.end(); ++u) {
			if(teams[side_-1].is_enemy(u->second.side()) && tiles_adjacent(loc,u->first)) {
				// Enemy spotted in adjacent tiles, check if we can see him.
				// Watch out to call invisible with see_all=true to avoid infinite recursive calls!
				if(see_all) {
					is_inv = false;
					break;
				} else if(!teams[side_-1].fogged(u->first)
				&& !u->second.invisible(u->first, units,teams,true)) {
					is_inv = false;
					break;
				}
			}
		}
	}

	if(see_all) {
		// Add to caches
		if(invisibility_cache_.empty()) {
			units_with_cache.push_back(this);
		}
		invisibility_cache_[loc] = is_inv;
	}

	return is_inv;
}

void unit::set_underlying_id() {
	if(underlying_id_ == 0){
		underlying_id_ = n_unit::id_manager::instance().next_id();
	}
}

void unit::clone()
{
	underlying_id_ = n_unit::id_manager::instance().next_fake_id();

}

int team_units(const unit_map& units, unsigned int side)
{
	int res = 0;
	for(unit_map::const_iterator i = units.begin(); i != units.end(); ++i) {
		if(i->second.side() == side) {
			++res;
		}
	}

	return res;
}

int team_upkeep(const unit_map& units, unsigned int side)
{
	int res = 0;
	for(unit_map::const_iterator i = units.begin(); i != units.end(); ++i) {
		if(i->second.side() == side) {
			res += i->second.upkeep();
		}
	}

	return res;
}

unit_map::const_iterator team_leader(unsigned int side, const unit_map& units)
{
	for(unit_map::const_iterator i = units.begin(); i != units.end(); ++i) {
		if(i->second.can_recruit() && i->second.side() == side) {
			return i;
		}
	}

	return units.end();
}

unit_map::iterator find_visible_unit(unit_map& units,
		const gamemap::location loc,
		const gamemap& map,
		  const std::vector<team>& teams, const team& current_team,
		bool see_all)
{
	unit_map::iterator u = units.find(loc);
	if(map.on_board(loc) && !see_all){
		if(u != units.end()){
			if(current_team.fogged(loc)){
				return units.end();
			}
			if(current_team.is_enemy(u->second.side()) &&
					u->second.invisible(loc,units,teams)) {
				return units.end();
			}
		}
	}
	return u;
}

unit_map::const_iterator find_visible_unit(const unit_map& units,
		const gamemap::location loc,
		const gamemap& map,
		const std::vector<team>& teams, const team& current_team,
		bool see_all)
{
	unit_map::const_iterator u = units.find(loc);
	if(map.on_board(loc) && !see_all){
		if(u != units.end()){
			if(current_team.fogged(loc)){
				return units.end();
			}
			if(current_team.is_enemy(u->second.side()) &&
					u->second.invisible(loc,units,teams)) {
				return units.end();
			}
		}
	}
	return u;
}

team_data calculate_team_data(const team& tm, int side, const unit_map& units)
{
	team_data res;
	res.units = team_units(units,side);
	res.upkeep = team_upkeep(units,side);
	res.villages = tm.villages().size();
	res.expenses = std::max<int>(0,res.upkeep - res.villages);
	res.net_income = tm.income() - res.expenses;
	res.gold = tm.gold();
	res.teamname = tm.user_team_name();
	return res;
}

temporary_unit_placer::temporary_unit_placer(unit_map& m, const gamemap::location& loc, unit& u)
	: m_(m), loc_(loc), temp_(m.extract(loc))
{
	u.clone();
	m.add(new std::pair<gamemap::location,unit>(loc,u));
}

temporary_unit_placer::~temporary_unit_placer()
{
	m_.erase(loc_);
	if(temp_) {
		m_.add(temp_);
	}
}

std::string unit::image_mods() const{
	std::stringstream modifier;
	if(flag_rgb_.size()){
		modifier << "~RC("<< flag_rgb_ << ">" << team::get_side_colour_index(side()) << ")";
	}
	if(image_mods_.size()){
		modifier << "~" << image_mods_;
	}
	return modifier.str();
}

const tportrait* unit::portrait(
		const unsigned size, const tportrait::tside side) const
{
	foreach(const tportrait& portrait, (type()->portraits())) {
		if(portrait.size == size
				&& (side ==  portrait.side || portrait.side == tportrait::BOTH)) {

			return &portrait;
		}
	}

	return NULL;
}

void unit::set_hidden(bool state) {
	hidden_ = state;
	if(!state) return;
	// We need to get rid of haloes immediately to avoid display glitches
	clear_haloes();
}
/**
 * advanceto
 * alignment
 * cost
 * experience
 * gender
 * hitpoints
 * level
 * max_attacks
 * max_experience
 * max_hitpoints
 * max_moves
 * movement
 * movement_type
 * race
 * random_traits
 * resting
 * undead_variation
 * upkeep
 * value
 * zoc
 * [attack]
 *         name
 *         type
 *         range
 *         damage
 *         number
 *         [specials]
 * 		*
 * 	[/special]
 * [/attack]
 * anything in: [abilities], [advance_from], [defense], [movement_cost], [resistance], [trait]
 * remove description, description_inactive, name, name_inactive from all tags under [abilities]
 * remove description from all tags under [specials]
 * remove description, male_name, female_name, name from [trait]
 **/
std::string get_checksum(const unit& u) {
	config unit_config;
	config wcfg;
	u.write(unit_config);
	const std::string main_keys[] =
		{ "advances_to",
		"alignment",
		"cost",
		"experience",
		"gender",
		"hitpoints",
		"ignore_race_traits",
		"ignore_global_traits",
		"level",
		"max_attacks",
		"max_experience",
		"max_hitpoints",
		"max_moves",
		"movement",
		"movement_type",
		"race",
		"random_traits",
		"resting",
		"undead_variation",
		"upkeep",
		"zoc",
		""};

	int i;

	for (i = 0; !main_keys[i].empty(); ++i)
	{
		wcfg[main_keys[i]] = unit_config[main_keys[i]];
	}
	const std::string attack_keys[] =
		{ "name",
	        "type",
        	"range",
	        "damage",
        	"number",
		""};
	const config::child_list& attacks = unit_config.get_children("attack");
	for (config::child_list::const_iterator att = attacks.begin(); att != attacks.end(); ++att)
	{
		config& child = wcfg.add_child("attack");
		for (i = 0; !attack_keys[i].empty(); ++i)
		{
			child[attack_keys[i]] = (**att)[attack_keys[i]];
		}
		const config::child_list& specials = (*att)->get_children("specials");

		for (config::child_list::const_iterator spec = specials.begin(); spec != specials.end(); ++spec)
		{
			config& child_spec = child.add_child("specials", **spec);
			child_spec.recursive_clear_value("description");

		}

	}

	const config::child_list& abilities = unit_config.get_children("abilities");
	for (config::child_list::const_iterator abi = abilities.begin(); abi != abilities.end(); ++abi)
	{
		config& child = wcfg.add_child("abilities", **abi);
		child.recursive_clear_value("description");
		child.recursive_clear_value("description_inactive");
		child.recursive_clear_value("name");
		child.recursive_clear_value("name_inactive");
	}

	const config::child_list& traits = unit_config.get_children("trait");
	for (config::child_list::const_iterator trait = traits.begin(); trait != traits.end(); ++trait)
	{
		config& child = wcfg.add_child("trait", **trait);
		child.recursive_clear_value("description");
		child.recursive_clear_value("male_name");
		child.recursive_clear_value("female_name");
		child.recursive_clear_value("name");
	}

	const std::string child_keys[] = {"advance_from", "defense", "movement_cost", "resistance",""};

	for  (i = 0; !child_keys[i].empty(); ++i)
	{
		const config::child_list& children = unit_config.get_children(child_keys[i]);
		for (config::child_list::const_iterator c = children.begin(); c != children.end(); ++c)
		{
			wcfg.add_child(child_keys[i], **c);
		}
	}
	DBG_UT << wcfg;

	return wcfg.hash();

	unit_config["controller"] = "";
	// Since the ai messes up the 'moves' attribute, ignore that for the checksum
	unit_config["moves"] = "";
	// Non-critical attributes to ignore.
	unit_config["alpha"] = "";
	unit_config["description"] = "";
	unit_config["die_sound"] = "";
	unit_config["ellipse"] = "";
	unit_config["facing"] = "";
	unit_config["flag_rgb"] = "";
	unit_config["image"] = "";
	unit_config["language_name"] = "";
	unit_config["name"] = "";
	unit_config["overlays"] = "";
	// Non-critical tags to ignore.
	unit_config.clear_children("comment");
	foreach(const std::string& tag_name, unit_animation::all_tag_names()) {
		unit_config.clear_children(tag_name);
	}

	return unit_config.hash();
}

