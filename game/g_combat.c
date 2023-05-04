/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// g_combat.c

#include "g_local.h"

/*
============
CanDamage

Returns true if the inflictor can directly damage the target.  Used for
explosions and melee attacks.
============
*/
qboolean CanDamage (edict_t *targ, edict_t *inflictor)
{
	vec3_t	dest;
	trace_t	trace;

// bmodels need special checking because their origin is 0,0,0
	if (targ->movetype == MOVETYPE_PUSH)
	{
		VectorAdd (targ->absmin, targ->absmax, dest);
		VectorScale (dest, 0.5, dest);
		trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
		if (trace.fraction == 1.0)
			return true;
		if (trace.ent == targ)
			return true;
		return false;
	}
	
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, targ->s.origin, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] += 15.0;
	dest[1] += 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] += 15.0;
	dest[1] -= 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] -= 15.0;
	dest[1] += 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] -= 15.0;
	dest[1] -= 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;


	return false;
}


/*
============
Killed
============
*/
void Killed (edict_t *targ, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (targ->health < -999)
		targ->health = -999;

	targ->enemy = attacker;

	if ((targ->svflags & SVF_MONSTER) && (targ->deadflag != DEAD_DEAD))
	{
//		targ->svflags |= SVF_DEADMONSTER;	// now treat as a different content type
		if (!(targ->monsterinfo.aiflags & AI_GOOD_GUY))
		{
			level.killed_monsters++;
			if (coop->value && attacker->client)
				attacker->client->resp.score++;
			// medics won't heal monsters that they kill themselves
			if (strcmp(attacker->classname, "monster_medic") == 0)
				targ->owner = attacker;
		}
	}

	if (targ->movetype == MOVETYPE_PUSH || targ->movetype == MOVETYPE_STOP || targ->movetype == MOVETYPE_NONE)
	{	// doors, triggers, etc
		targ->die (targ, inflictor, attacker, damage, point);
		return;
	}

	if ((targ->svflags & SVF_MONSTER) && (targ->deadflag != DEAD_DEAD))
	{
		targ->touch = NULL;
		monster_death_use (targ);
	}

	targ->die (targ, inflictor, attacker, damage, point);
}


/*
================
SpawnDamage
================
*/
void SpawnDamage (int type, vec3_t origin, vec3_t normal, int damage)
{
	if (damage > 255)
		damage = 255;
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (type);
//	gi.WriteByte (damage);
	gi.WritePosition (origin);
	gi.WriteDir (normal);
	gi.multicast (origin, MULTICAST_PVS);
}


/*
============
T_Damage

targ		entity that is being damaged
inflictor	entity that is causing the damage
attacker	entity that caused the inflictor to damage targ
	example: targ=monster, inflictor=rocket, attacker=player

dir			direction of the attack
point		point at which the damage is being inflicted
normal		normal vector from that point
damage		amount of damage being inflicted
knockback	force to be applied against targ as a result of the damage

dflags		these flags are used to control how T_Damage works
	DAMAGE_RADIUS			damage was indirect (from a nearby explosion)
	DAMAGE_NO_ARMOR			armor does not protect from this damage
	DAMAGE_ENERGY			damage is from an energy based weapon
	DAMAGE_NO_KNOCKBACK		do not affect velocity, just view angles
	DAMAGE_BULLET			damage is from a bullet (used for ricochets)
	DAMAGE_NO_PROTECTION	kills godmode, armor, everything
============
*/
static int CheckPowerArmor (edict_t *ent, vec3_t point, vec3_t normal, int damage, int dflags)
{
	gclient_t	*client;
	int			save;
	int			power_armor_type;
	int			index;
	int			damagePerCell;
	int			pa_te_type;
	int			power;
	int			power_used;

	if (!damage)
		return 0;

	client = ent->client;

	if (dflags & DAMAGE_NO_ARMOR)
		return 0;

	if (client)
	{
		power_armor_type = PowerArmorType (ent);
		if (power_armor_type != POWER_ARMOR_NONE)
		{
			index = ITEM_INDEX(FindItem("Cells"));
			power = client->pers.inventory[index];
		}
	}
	else if (ent->svflags & SVF_MONSTER)
	{
		power_armor_type = ent->monsterinfo.power_armor_type;
		power = ent->monsterinfo.power_armor_power;
	}
	else
		return 0;

	if (power_armor_type == POWER_ARMOR_NONE)
		return 0;
	if (!power)
		return 0;

	if (power_armor_type == POWER_ARMOR_SCREEN)
	{
		vec3_t		vec;
		float		dot;
		vec3_t		forward;

		// only works if damage point is in front
		AngleVectors (ent->s.angles, forward, NULL, NULL);
		VectorSubtract (point, ent->s.origin, vec);
		VectorNormalize (vec);
		dot = DotProduct (vec, forward);
		if (dot <= 0.3)
			return 0;

		damagePerCell = 1;
		pa_te_type = TE_SCREEN_SPARKS;
		damage = damage / 3;
	}
	else
	{
		damagePerCell = 2;
		pa_te_type = TE_SHIELD_SPARKS;
		damage = (2 * damage) / 3;
	}

	save = power * damagePerCell;
	if (!save)
		return 0;
	if (save > damage)
		save = damage;

	SpawnDamage (pa_te_type, point, normal, save);
	ent->powerarmor_time = level.time + 0.2;

	power_used = save / damagePerCell;

	if (client)
		client->pers.inventory[index] -= power_used;
	else
		ent->monsterinfo.power_armor_power -= power_used;
	return save;
}

static int CheckArmor (edict_t *ent, vec3_t point, vec3_t normal, int damage, int te_sparks, int dflags)
{
	gclient_t	*client;
	int			save;
	int			index;
	gitem_t		*armor;

	if (!damage)
		return 0;

	client = ent->client;

	if (!client)
		return 0;

	if (dflags & DAMAGE_NO_ARMOR)
		return 0;

	index = ArmorIndex (ent);
	if (!index)
		return 0;

	armor = GetItemByIndex (index);

	if (dflags & DAMAGE_ENERGY)
		save = ceil(((gitem_armor_t *)armor->info)->energy_protection*damage);
	else
		save = ceil(((gitem_armor_t *)armor->info)->normal_protection*damage);
	if (save >= client->pers.inventory[index])
		save = client->pers.inventory[index];

	if (!save)
		return 0;

	client->pers.inventory[index] -= save;
	SpawnDamage (te_sparks, point, normal, save);

	return save;
}

void M_ReactToDamage (edict_t *targ, edict_t *attacker)
{
	if (!(attacker->client) && !(attacker->svflags & SVF_MONSTER))
		return;

	if (attacker == targ || attacker == targ->enemy)
		return;

	// if we are a good guy monster and our attacker is a player
	// or another good guy, do not get mad at them
	if (targ->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		if (attacker->client || (attacker->monsterinfo.aiflags & AI_GOOD_GUY))
			return;
	}

	// we now know that we are not both good guys

	// if attacker is a client, get mad at them because he's good and we're not
	if (attacker->client)
	{
		targ->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

		// this can only happen in coop (both new and old enemies are clients)
		// only switch if can't see the current enemy
		if (targ->enemy && targ->enemy->client)
		{
			if (visible(targ, targ->enemy))
			{
				targ->oldenemy = attacker;
				return;
			}
			targ->oldenemy = targ->enemy;
		}
		targ->enemy = attacker;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
		return;
	}

	// it's the same base (walk/swim/fly) type and a different classname and it's not a tank
	// (they spray too much), get mad at them
	if (((targ->flags & (FL_FLY|FL_SWIM)) == (attacker->flags & (FL_FLY|FL_SWIM))) &&
		 (strcmp (targ->classname, attacker->classname) != 0) &&
		 (strcmp(attacker->classname, "monster_tank") != 0) &&
		 (strcmp(attacker->classname, "monster_supertank") != 0) &&
		 (strcmp(attacker->classname, "monster_makron") != 0) &&
		 (strcmp(attacker->classname, "monster_jorg") != 0) )
	{
		if (targ->enemy && targ->enemy->client)
			targ->oldenemy = targ->enemy;
		targ->enemy = attacker;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
	}
	// if they *meant* to shoot us, then shoot back
	else if (attacker->enemy == targ)
	{
		if (targ->enemy && targ->enemy->client)
			targ->oldenemy = targ->enemy;
		targ->enemy = attacker;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
	}
	// otherwise get mad at whoever they are mad at (help our buddy) unless it is us!
	else if (attacker->enemy && attacker->enemy != targ)
	{
		if (targ->enemy && targ->enemy->client)
			targ->oldenemy = targ->enemy;
		targ->enemy = attacker->enemy;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
	}
}

qboolean CheckTeamDamage (edict_t *targ, edict_t *attacker)
{
		//FIXME make the next line real and uncomment this block
		// if ((ability to damage a teammate == OFF) && (targ's team == attacker's team))
	return false;
}

//HERE BEGIN
/*
void blank_think(edict_t* self) {
	self->nextthink = level.time + FRAMETIME;
	self->s.frame++;
	if (self->s.frame == 5)
		self->think = G_FreeEdict;
}
*/
qboolean checkSgrenade = false;
qboolean checkGrenade = false;

void poison_enemy(edict_t* enemy, edict_t* attacker, int duration, int dmg) {
	enemy->poison_time = level.time + duration;
	enemy->poison_damage = dmg / duration;
	enemy->poison_attacker = attacker;
}

void poison_think(edict_t* enemy) {
	if (level.time > enemy->poison_time) {
		enemy->poison_time = 0;
		enemy->poison_damage = 0;
		enemy->poison_attacker = NULL;
		enemy->think = monster_think;
		enemy->nextthink = level.time + FRAMETIME;
		return;
	}
	checkGrenade = true;
	T_Damage(enemy, enemy->poison_attacker, enemy->poison_attacker, vec3_origin, enemy->s.origin, vec3_origin, enemy->poison_damage, 0, 0, MOD_POISON);
	enemy->nextthink = level.time + 1;
}

void push_think(edict_t* enemy) {
	vec3_t dir, push;
	float dist;
	if (level.time >= enemy->push_time) {
		enemy->push_time = 0;
		enemy->push_attacker = NULL;
		enemy->push_inflictor = NULL;
		enemy->think = monster_think;
		enemy->nextthink = level.time + FRAMETIME;
		return;
	}
	VectorSubtract(enemy->s.origin, enemy->push_attacker->s.origin, dir);
	dist = VectorNormalize(dir);
	float force = 2000.0f;
	VectorScale(dir, force, push);
	//vec3_t mogus = { 1, 1, 1 };
	checkSgrenade = true;
	T_Damage(enemy, enemy->push_attacker, enemy->push_attacker, vec3_origin, enemy->s.origin, vec3_origin, 10, 50, 0, MOD_PUSH);
	VectorMA(enemy->velocity, 1.0f, push, enemy->velocity);
	gi.linkentity(enemy);
	enemy->nextthink = level.time + 1;
	/*
	T_Damage(enemy, enemy->poison_attacker, enemy->poison_attacker, vec3_origin, enemy->s.origin, vec3_origin, 1, 10, 0, MOD_PUSH);
	gi.linkentity(enemy);
	enemy->think = monster_think;
	enemy->nextthink = level.time + FRAMETIME;
	*/
}

void my_think(edict_t* self) {
	self->nextthink = level.time + FRAMETIME;
}

void rand_think(edict_t* self) {
	if (level.time > self->stun_time) {
		self->stun_time = 0;
		self->think = monster_think;
		self->nextthink = level.time + FRAMETIME;
	}
	ai_rand_move(self, 50);
	gi.linkentity(self);
	self->nextthink = level.time + 1;
}
//HERE END


void T_Damage (edict_t *targ, edict_t *inflictor, edict_t *attacker, vec3_t dir, vec3_t point, vec3_t normal, int damage, int knockback, int dflags, int mod)
{
	gclient_t	*client;
	int			take;
	int			save;
	int			asave;
	int			psave;
	int			te_sparks;

	if (!targ->takedamage)
		return;
	// friendly fire avoidance
	// if enabled you can't hurt teammates (but you can hurt yourself)
	// knockback still occurs
	if ((targ != attacker) && ((deathmatch->value && ((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS))) || coop->value))
	{
		if (OnSameTeam (targ, attacker))
		{
			if ((int)(dmflags->value) & DF_NO_FRIENDLY_FIRE)
				damage = 0;
			else
				mod |= MOD_FRIENDLY_FIRE;
		}
	}
	meansOfDeath = mod;

	// easy mode takes half damage
	if (skill->value == 0 && deathmatch->value == 0 && targ->client)
	{
		damage *= 0.5;
		if (!damage)
			damage = 1;
	}

	client = targ->client;

	if (dflags & DAMAGE_BULLET)
		te_sparks = TE_BULLET_SPARKS;
	else
		te_sparks = TE_SPARKS;

	VectorNormalize(dir);

// bonus damage for suprising a monster
	if (!(dflags & DAMAGE_RADIUS) && (targ->svflags & SVF_MONSTER) && (attacker->client) && (!targ->enemy) && (targ->health > 0))
		damage *= 2;

	if (targ->flags & FL_NO_KNOCKBACK)
		knockback = 0;

// figure momentum add
	if (!(dflags & DAMAGE_NO_KNOCKBACK))
	{
		if ((knockback) && (targ->movetype != MOVETYPE_NONE) && (targ->movetype != MOVETYPE_BOUNCE) && (targ->movetype != MOVETYPE_PUSH) && (targ->movetype != MOVETYPE_STOP))
		{
			vec3_t	kvel;
			float	mass;

			if (targ->mass < 50)
				mass = 50;
			else
				mass = targ->mass;

			if (targ->client  && attacker == targ)
				VectorScale (dir, 1600.0 * (float)knockback / mass, kvel);	// the rocket jump hack...
			else
				VectorScale (dir, 500.0 * (float)knockback / mass, kvel);

			VectorAdd (targ->velocity, kvel, targ->velocity);
		}
	}

	take = damage;
	save = 0;

	// check for godmode
	if ( (targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION) )
	{
		take = 0;
		save = damage;
		SpawnDamage (te_sparks, point, normal, save);
	}

	// check for invincibility
	if ((client && client->invincible_framenum > level.framenum ) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		if (targ->pain_debounce_time < level.time)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex("items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_time = level.time + 2;
		}
		take = 0;
		save = damage;
	}

	psave = CheckPowerArmor (targ, point, normal, take, dflags);
	take -= psave;

	asave = CheckArmor (targ, point, normal, take, te_sparks, dflags);
	take -= asave;

	//treat cheat/powerup savings the same as armor
	asave += save;

	// team damage avoidance
	if (!(dflags & DAMAGE_NO_PROTECTION) && CheckTeamDamage (targ, attacker))
		return;

// do the damage
	//gi.cprintf(inflictor->owner, PRINT_HIGH, "got here before T_Damage\n");
	//gi.cprintf(inflictor->owner, PRINT_HIGH, itoa(take, num, 10));

	if (take)
	{
		//gi.cprintf(inflictor->owner, PRINT_HIGH, "got here in T_Damage -> take\n");
		if ((targ->svflags & SVF_MONSTER) || (client))
			SpawnDamage (TE_BLOOD, point, normal, take);
		else
			SpawnDamage (te_sparks, point, normal, take);

		//HERE BEGIN
		take = 0;
		if (mod == MOD_POISON) {
			take = targ->poison_damage;
		}
		else if (mod == MOD_PUSH) {
			take = 0;
		}
		else if (mod == MOD_BLASTER) {
			take = 5;
		}
		else if (mod == MOD_SHOTGUN) {
			take = 1;
		}
		else if (mod == MOD_HYPERBLASTER) {
			take = 1;
		}
		else if (mod == MOD_MACHINEGUN) {
			take = 1;
		}
		else if (mod == MOD_CHAINGUN) {
			take = 1;
		}
		else if (mod == MOD_SSHOTGUN) {
			take = 1;
		}
		else if (mod == MOD_RAILGUN) {
			take = 100;
		}
		else if (mod == MOD_HANDSGRENADE || mod == MOD_SHG_SPLASH || mod == MOD_SG_SPLASH || mod == MOD_SGRENADE || mod == MOD_HELD_SGRENADE) {
			targ->push_attacker = attacker;
			targ->push_inflictor = inflictor;
			targ->push_time = level.time + 1;
			targ->think = push_think;
		}
		else if (mod == MOD_HANDCGRENADE || mod == MOD_CHG_SPLASH ||mod == MOD_CG_SPLASH || mod == MOD_CGRENADE || mod == MOD_HELD_CGRENADE) {
			take = 0;
			targ->stun_time = level.time + 5;
			targ->think = rand_think;
			//targ->nextthink = level.time + 1;
			//float angle = targ->s.angles[YAW] + (random() - 0.5) * 180;
			//targ->s.angles[YAW] = angle;
			
			/*
			if (!targ->think) {
				targ->think = my_think;
			}
			targ->nextthink = level.time + 5;
			*/
			//gi.linkentity(targ);
			
		}
		else if (mod == MOD_BFG_BLAST || mod == MOD_BFG_EFFECT || mod == MOD_BFG_LASER) {

			take = 0;
			if (!targ->think) {
				targ->think = my_think;
			}
			targ->nextthink = level.time + 999999;
			gi.linkentity(targ);


			//poison_enemy(targ, attacker, 2, 10);
			//targ->think = poison_think;

			//poison_enemy(targ, attacker, 3, 10);
			//if (!targ->think)
			//	targ->think = my_think;
			//targ->think = poison_think;
			//targ->pain(targ, NULL, 0, targ->poison_damage);
		}
		else if (mod == MOD_HANDGRENADE || mod == MOD_HG_SPLASH || mod == MOD_G_SPLASH || mod == MOD_GRENADE || mod == MOD_HELD_GRENADE || strcmp(inflictor->classname, "hgrenade")) {
			/*
			take = 100;
			if (!targ->think) {
				targ->think = my_think;
			}
			targ->nextthink = level.time + 5;
			gi.linkentity(targ);
			*/
			poison_enemy(targ, attacker, 2, 10);
			targ->think = poison_think;
		}
		else {
			take = 100;
		}
		//if (attacker->package)
		//targ->dmg_multiplier = 1;
		if (attacker->client) {
			targ->health = targ->health - (take + attacker->enemy_dmg_mult);
		}
		else {
			targ->health = targ->health - (take * targ->dmg_multiplier);
		}

			// attacker is an enemy
		//targ->health = targ->health - (take * targ->dmg_multiplier);
		//HERE END
		
		if (targ->health <= 0 && attacker->client)
		{
			if ((targ->svflags & SVF_MONSTER) || (client))
				targ->flags |= FL_NO_KNOCKBACK;
			Killed (targ, inflictor, attacker, take, point);

			char kills[5];
			attacker->enemy_num += 1;
			gi.cprintf(attacker, PRINT_HIGH, "%s\n", itoa(attacker->enemy_num, kills, 10));
			G_FreeEdict(targ);
			/*
			else {
				char kills[5];
				attacker->enemy->enemy_num += 1;
				gi.cprintf(attacker->enemy, PRINT_HIGH, "%s\n", itoa(attacker->enemy_num, kills, 10));
				if (!targ->client)
					G_FreeEdict(targ);
			}
			*/
			if (attacker->enemy_num > 0 && attacker->enemy_num < 12) {
				attacker->wave_flag = 1;
			}
			else if (attacker->enemy_num >= 12) {
				attacker->wave_flag = 1;
				attacker->wave_flag2 = 2;
			}
			return;
		}
	}

	if (targ->svflags & SVF_MONSTER)
	{
		M_ReactToDamage (targ, attacker);
		if (!(targ->monsterinfo.aiflags & AI_DUCKED) && (take))
		{
			targ->pain (targ, attacker, knockback, take);
			// nightmare mode monsters don't go into pain frames often
			if (skill->value == 3)
				targ->pain_debounce_time = level.time + 5;
		}
	}
	else if (client)
	{
		if (!(targ->flags & FL_GODMODE) && (take))
			targ->pain (targ, attacker, knockback, take);
	}
	else if (take)
	{
		if (targ->pain)
			targ->pain (targ, attacker, knockback, take);
	}

	// add to the damage inflicted on a player this frame
	// the total will be turned into screen blends and view angle kicks
	// at the end of the frame
	if (client)
	{
		client->damage_parmor += psave;
		client->damage_armor += asave;
		client->damage_blood += take;
		client->damage_knockback += knockback;
		VectorCopy (point, client->damage_from);
	}
}


/*
============
T_RadiusDamage
============
*/

void T_RadiusDamage (edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod)
{
	float	points;
	edict_t	*ent = NULL;
	vec3_t	v;
	vec3_t	dir;

	while ((ent = findradius(ent, inflictor->s.origin, radius)) != NULL)
	{
		if (ent == ignore)
			continue;
		if (!ent->takedamage)
			continue;
		//strcmp(inflictor->classname, "hgrenades")
		//mod == MOD_HANDGRENADE || mod == MOD_HG_SPLASH || mod == MOD_G_SPLASH || mod == MOD_GRENADE || mod == MOD_HELD_GRENADE
		if (strcmp(inflictor->classname, "hgrenade")) {
			points = 0;
		}
		else if (mod == MOD_HANDCGRENADE || mod == MOD_CHG_SPLASH || mod == MOD_CG_SPLASH || mod == MOD_CGRENADE || mod == MOD_HELD_CGRENADE) {
			inflictor->classname = "cgrenade";
			points = 0;
		}
		else if (mod == MOD_HANDSGRENADE || mod == MOD_SHG_SPLASH || mod == MOD_SG_SPLASH || mod == MOD_SGRENADE || mod == MOD_HELD_SGRENADE) {
			inflictor->classname = "sgrenade";
			points = 0;
		}
		else if (mod == MOD_BFG_BLAST) {
			points = 0;
			//ent->think = my_think;
			//ent->think = blank_think;
			//ent->nextthink = level.time + 4;
		}
		else if (mod == MOD_BFG_EFFECT) {
			points = 0;
			//ent->think = my_think;
		}
		else {
			points = 100;
		}

		//ent->nextthink = level.time + 5;

		//VectorAdd(ent->mins, ent->maxs, v);
		//VectorMA(ent->s.origin, 0.5, v, v);
		//VectorSubtract(inflictor->s.origin, v, v);
		//points = damage - 0.5 * VectorLength(v);
		if (ent == attacker)
			points = points * 0.5;
		if (points > 0)
		{
			if (CanDamage(ent, inflictor))
			{
				VectorSubtract(ent->s.origin, inflictor->s.origin, dir);
				//HERE BEGIN
				//Changed damaged value to 0, was "(int)points"
				//EDIT, changed damage value back to "(int)points"
				T_Damage(ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin, 100, (int)points, DAMAGE_RADIUS, mod);
				//HERE BEGIN
			}
		}
		//HERE BEGIN
		if (points == 0) {
			if (CanDamage(ent, inflictor))
			{
				VectorSubtract(ent->s.origin, inflictor->s.origin, dir);
				T_Damage(ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin, 100, (int)points, DAMAGE_RADIUS, mod);
				//HERE BEGIN
				//Changed damaged value to 0, was "(int)points"
				//EDIT, changed damage value back to "(int)points"
				//gi.cprintf(ent->owner, PRINT_HIGH, "GOT HERE\n");
				//HERE BEGIN
			}
		}
		//HERE END
	}
}
