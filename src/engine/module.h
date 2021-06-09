/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
/* libavs -- simple sync engine
 *
 * Engine modules
 *
 * To make extending the engine easier, we group code into modules with
 * a common startup and shutdown structure. If you write a new module,
 * add these functions, define a struct engine_module and then wire it up
 * in engine/module.c.
 *
 * Any loaded module will have a state object in engine->modulel. If you
 * need data for your operation, create a struct for your module and add
 * it to struct engine. This way, we don't have to go through list and
 * also can use this to quickly check if your module is loaded, as the
 * pointer will stay at NULL.
 */

struct engine;
struct engine_module_state;


typedef int (engine_module_init_h)(void);
typedef int (engine_module_alloc_h)(struct engine *engine,
				    struct engine_module_state *state);
typedef void (engine_module_state_h)(struct engine *engine,
				     struct engine_module_state *state);
typedef void (engine_module_caughtup_h)(struct engine *engine);
typedef void (engine_module_close_h)(void);

struct engine_module {
	struct le le;

	/* Name for debugging.  */
	char *name;

	/* Called at program initialisation.  */
	engine_module_init_h *inith;

	/* Called at engine allocation.
	 *
	 * You'll receive a readily set up state object. If you want to be
	 * part of the engine, append it to engine->modulel. If you don't
	 * just mem_deref it.
	 */
	engine_module_alloc_h *alloch;

	/* Called when login has finished. Do your thing. Once you are
	 * finished, set state->state to ENGINE_STATE_ACTIVE and call
	 * engine_active_handler().
	 *
	 * You can leave this at NULL if you don't need the function.
	 */
	engine_module_state_h *startuph;

	/* Called when the entire engine goes into active. No need to do
	 * anything.
	 */
	engine_module_state_h *activeh;

	/* Called when catching up with events is finished. It will not
	 * be called when catching up didn't happen at all.
	 */
	engine_module_caughtup_h *caughtuph;

	/* Called when engine shutdown has been initiated. Once your are
	 * done shutting down, set state->state to ENGINE_STATE_DEAD
	 * and call engine_shutdown_handler().
	 *
	 * You can leave this at NULL if you don't need the function.
	 */
	engine_module_state_h *shuth;

	/* Called at program shutdown.
	 */
	engine_module_close_h *closeh;
};

void engine_init_modules(void);
void engine_close_modules(void);
const struct list *engine_get_modules(void);
