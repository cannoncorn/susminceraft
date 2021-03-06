/*
content_sao.h
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
*/

/*
This file is part of Freeminer.

Freeminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Freeminer  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Freeminer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CONTENT_SAO_HEADER
#define CONTENT_SAO_HEADER

#include "serverobject.h"
#include "itemgroup.h"
#include "object_properties.h"
#include "constants.h"

class UnitSAO: public ServerActiveObject
{
public:
	UnitSAO(ServerEnvironment *env, v3f pos):
			ServerActiveObject(env, pos),
			m_hp(-1), m_yaw(0) {}
	virtual ~UnitSAO() {}

	virtual void setYaw(const float yaw) { m_yaw = yaw; }
	float getYaw() const { return m_yaw; };
	f32 getRadYaw() const { return m_yaw * core::DEGTORAD; }
	// Deprecated
	f32 getRadYawDep() const { return (m_yaw + 90.) * core::DEGTORAD; }

	s16 getHP() const { return m_hp; }
	// Use a function, if isDead can be defined by other conditions
	bool isDead() const { return m_hp == 0; }
protected:
	std::atomic_ushort m_hp;
	float m_yaw;
};

/*
	LuaEntitySAO needs some internals exposed.
*/

class LuaEntitySAO : public UnitSAO
{
public:
	LuaEntitySAO(ServerEnvironment *env, v3f pos,
	             const std::string &name, const std::string &state);
	~LuaEntitySAO();
	ActiveObjectType getType() const
	{ return ACTIVEOBJECT_TYPE_LUAENTITY; }
	ActiveObjectType getSendType() const
	{ return ACTIVEOBJECT_TYPE_GENERIC; }
	virtual void addedToEnvironment(u32 dtime_s);
	static ServerActiveObject* create(ServerEnvironment *env, v3f pos,
			const std::string &data);
	bool isAttached();
	void step(float dtime, bool send_recommended);
	std::string getClientInitializationData(u16 protocol_version);
	std::string getStaticData();
	int punch(v3f dir,
			const ToolCapabilities *toolcap=NULL,
			ServerActiveObject *puncher=NULL,
			float time_from_last_punch=1000000);
	void rightClick(ServerActiveObject *clicker);
	void setPos(const v3f &pos);
	void moveTo(v3f pos, bool continuous);
	float getMinimumSavedMovement();
	std::string getDescription();
	void setHP(s16 hp);
	s16 getHP() const;
	void setArmorGroups(const ItemGroupList &armor_groups);
	ItemGroupList getArmorGroups();
	void setAnimation(v2f frame_range, float frame_speed, float frame_blend, bool frame_loop);
	void getAnimation(v2f *frame_range, float *frame_speed, float *frame_blend, bool *frame_loop);
	void setBonePosition(const std::string &bone, v3f position, v3f rotation);
	void getBonePosition(const std::string &bone, v3f *position, v3f *rotation);
	void setAttachment(int parent_id, const std::string &bone, v3f position, v3f rotation);
	void getAttachment(int *parent_id, std::string *bone, v3f *position, v3f *rotation);
	void addAttachmentChild(int child_id);
	void removeAttachmentChild(int child_id);
	UNORDERED_SET<int> getAttachmentChildIds();
	ObjectProperties* accessObjectProperties();
	void notifyObjectPropertiesModified();
	/* LuaEntitySAO-specific */
	void setVelocity(v3f velocity);
	v3f getVelocity();
	void setAcceleration(v3f acceleration);
	v3f getAcceleration();

	void setTextureMod(const std::string &mod);
	void setSprite(v2s16 p, int num_frames, float framelength,
			bool select_horiz_by_yawpitch);
	std::string getName();
	bool getCollisionBox(aabb3f *toset);
	bool collideWithObjects();
protected:
	std::string getPropertyPacket();
	void sendPosition(bool do_interpolate, bool is_movement_end);

	std::string m_init_name;
	std::string m_init_state;
	bool m_registered;

public:
	struct ObjectProperties m_prop;

	//std::atomic_ushort m_hp;
	v3f m_velocity;
	v3f m_acceleration;

private:
	ItemGroupList m_armor_groups;

	std::atomic_bool m_properties_sent;
	float m_last_sent_yaw;
	v3f m_last_sent_position;
	v3f m_last_sent_velocity;
	float m_last_sent_position_timer;
	float m_last_sent_move_precision;
	bool m_armor_groups_sent;

	v2f m_animation_range;
	float m_animation_speed;
	float m_animation_blend;
	bool m_animation_loop;
	bool m_animation_sent;

	UNORDERED_MAP<std::string, core::vector2d<v3f> > m_bone_position;
	bool m_bone_position_sent;

	int m_attachment_parent_id;
	UNORDERED_SET<int> m_attachment_child_ids;
	std::string m_attachment_bone;
	v3f m_attachment_position;
	v3f m_attachment_rotation;
	bool m_attachment_sent;
};

/*
	PlayerSAO needs some internals exposed.
*/

class LagPool
{
	float m_pool;
	float m_max;
public:
	LagPool(): m_pool(15), m_max(15)
	{}
	void setMax(float new_max)
	{
		m_max = new_max;
		if(m_pool > new_max)
			m_pool = new_max;
	}
	void add(float dtime)
	{
		m_pool -= dtime;
		if(m_pool < 0)
			m_pool = 0;
	}
	bool grab(float dtime)
	{
		if(dtime <= 0)
			return true;
		if(m_pool + dtime > m_max)
			return false;
		m_pool += dtime;
		return true;
	}
};

typedef UNORDERED_MAP<std::string, std::string> PlayerAttributes;
class RemotePlayer;

class PlayerSAO : public UnitSAO
{
public:
	PlayerSAO(ServerEnvironment *env_, u16 peer_id_, bool is_singleplayer);
	~PlayerSAO();
	ActiveObjectType getType() const
	{ return ACTIVEOBJECT_TYPE_PLAYER; }
	ActiveObjectType getSendType() const
	{ return ACTIVEOBJECT_TYPE_GENERIC; }
	std::string getDescription();

	/*
		Active object <-> environment interface
	*/

	void addedToEnvironment(u32 dtime_s);
	void removingFromEnvironment();
	bool isStaticAllowed() const;
	std::string getClientInitializationData(u16 protocol_version);
	std::string getStaticData();
	bool isAttached();
	void step(float dtime, bool send_recommended);
	void setBasePosition(const v3f &position);
	void setPos(const v3f &pos);
	void moveTo(v3f pos, bool continuous);
	void addSpeed(v3f);
	void setYaw(const float yaw);
	// Data should not be sent at player initialization
	void setYawAndSend(const float yaw);
	void setPitch(const float pitch);
	// Data should not be sent at player initialization
	void setPitchAndSend(const float pitch);
	f32 getPitch() const { return m_pitch; }
	f32 getRadPitch() const { return m_pitch * core::DEGTORAD; }
	// Deprecated
	f32 getRadPitchDep() const { return -1.0 * m_pitch * core::DEGTORAD; }
	void setFov(const float pitch);
	f32 getFov() const { return m_fov; }
	void setWantedRange(const s16 range);
	s16 getWantedRange() const { return m_wanted_range; }

	/*
		Interaction interface
	*/

	int punch(v3f dir,
		const ToolCapabilities *toolcap,
		ServerActiveObject *puncher,
		float time_from_last_punch);
	void rightClick(ServerActiveObject *clicker);
	void setHP(s16 hp);
	void setHPRaw(s16 hp) { m_hp = hp; }
	s16 readDamage();
	u16 getBreath() const { return m_breath; }
	void setBreath(const u16 breath);
	void setArmorGroups(const ItemGroupList &armor_groups);
	ItemGroupList getArmorGroups();
	void setAnimation(v2f frame_range, float frame_speed, float frame_blend, bool frame_loop);
	void getAnimation(v2f *frame_range, float *frame_speed, float *frame_blend, bool *frame_loop);
	void setBonePosition(const std::string &bone, v3f position, v3f rotation);
	void getBonePosition(const std::string &bone, v3f *position, v3f *rotation);
	void setAttachment(int parent_id, const std::string &bone, v3f position, v3f rotation);
	void getAttachment(int *parent_id, std::string *bone, v3f *position, v3f *rotation);
	void addAttachmentChild(int child_id);
	void removeAttachmentChild(int child_id);
	UNORDERED_SET<int> getAttachmentChildIds();
	ObjectProperties* accessObjectProperties();
	void notifyObjectPropertiesModified();

	/*
		Inventory interface
	*/

	Inventory* getInventory();
	const Inventory* getInventory() const;
	InventoryLocation getInventoryLocation() const;
	std::string getWieldList() const;
	ItemStack getWieldedItem() const;
	bool setWieldedItem(const ItemStack &item);
	int getWieldIndex() const;
	void setWieldIndex(int i);

	/*
		Modding interface
	*/
	inline void setExtendedAttribute(const std::string &attr, const std::string &value)
	{
		m_extra_attributes[attr] = value;
		m_extended_attributes_modified = true;
	}

	inline bool getExtendedAttribute(const std::string &attr, std::string *value)
	{
		if (m_extra_attributes.find(attr) == m_extra_attributes.end())
			return false;

		*value = m_extra_attributes[attr];
		return true;
	}

	inline const PlayerAttributes &getExtendedAttributes()
	{
		return m_extra_attributes;
	}

	inline bool extendedAttributesModified() const
	{
		return m_extended_attributes_modified;
	}

	inline void setExtendedAttributeModified(bool v)
	{
		m_extended_attributes_modified = v;
	}

	/*
		PlayerSAO-specific
	*/

	void disconnected();

	RemotePlayer *getPlayer() { return m_player; }
	u16 getPeerID() const { return m_peer_id; }

	// Cheat prevention

	v3f getLastGoodPosition() const
	{
		return m_last_good_position;
	}
	float resetTimeFromLastPunch()
	{
		auto lock = lock_unique_rec();
		float r = m_time_from_last_punch;
		m_time_from_last_punch = 0.0;
		return r;
	}
	void noCheatDigStart(v3s16 p)
	{
		auto lock = lock_unique_rec();
		m_nocheat_dig_pos = p;
		m_nocheat_dig_time = 0;
	}
	v3s16 getNoCheatDigPos()
	{
		return m_nocheat_dig_pos;
	}
	float getNoCheatDigTime()
	{
		return m_nocheat_dig_time;
	}
	void noCheatDigEnd()
	{
		m_nocheat_dig_pos = v3s16(32767, 32767, 32767);
	}
	LagPool& getDigPool()
	{
		return m_dig_pool;
	}
	// Returns true if cheated
	bool checkMovementCheat();

	// Other

	void updatePrivileges(const std::set<std::string> &privs,
			bool is_singleplayer)
	{
		m_privs = privs;
		m_is_singleplayer = is_singleplayer;
	}

	bool getCollisionBox(aabb3f *toset);
	bool collideWithObjects();

	void initialize(RemotePlayer *player, const std::set<std::string> &privs);

	v3f getEyePosition() const { return m_base_position + getEyeOffset(); }
	v3f getEyeOffset() const { return v3f(0, BS * 1.625f, 0); }

private:
	std::string getPropertyPacket();
	void unlinkPlayerSessionAndSave();

	RemotePlayer *m_player;
	u16 m_peer_id;
	Inventory *m_inventory;
	s16 m_damage;

	// Cheat prevention
	LagPool m_dig_pool;
	LagPool m_move_pool;
public:
	v3f m_last_good_position;
	std::atomic_uint m_ms_from_last_respawn;
private:
	float m_time_from_last_punch;
	v3s16 m_nocheat_dig_pos;
	float m_nocheat_dig_time;

	int m_wield_index;
	std::atomic_bool m_position_not_sent;
	ItemGroupList m_armor_groups;
	std::atomic_bool m_armor_groups_sent;

	std::atomic_bool m_properties_sent;
	struct ObjectProperties m_prop;
	// Cached privileges for enforcement
	std::set<std::string> m_privs;
	bool m_is_singleplayer;

	v2f m_animation_range;
	float m_animation_speed;
	float m_animation_blend;
	bool m_animation_loop;
	std::atomic_bool m_animation_sent;

	// Stores position and rotation for each bone name
	UNORDERED_MAP<std::string, core::vector2d<v3f> > m_bone_position;
	bool m_bone_position_sent;

	int m_attachment_parent_id;
	UNORDERED_SET<int> m_attachment_child_ids;
	std::string m_attachment_bone;
	v3f m_attachment_position;
	v3f m_attachment_rotation;
	bool m_attachment_sent;
	u16 m_breath;
	f32 m_pitch;
	f32 m_fov;
	s16 m_wanted_range;

	PlayerAttributes m_extra_attributes;
	bool m_extended_attributes_modified;
public:
	float m_physics_override_speed;
	float m_physics_override_jump;
	float m_physics_override_gravity;
	bool m_physics_override_sneak;
	bool m_physics_override_sneak_glitch;
	std::atomic_bool m_physics_override_sent;
};

#endif
