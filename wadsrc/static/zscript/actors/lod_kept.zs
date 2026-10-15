// Legend of Doom 3DS: what the kept ZScript and the mod need from the Heretic and Strife files that
// are not compiled: BossSpot and BloodySkull (compile-time defaults of Actor.A_Teleport and
// PlayerPawn.A_SkullPop) and A_AlertMonsters. Copied verbatim (see zscript.txt).

class BossSpot : SpecialSpot
{
	Default
	{
		+INVISIBLE
	}
}

class BloodySkull : PlayerChunk
{
	Default
	{
		Radius 4;
		Height 4;
		Gravity 0.125;
		+NOBLOCKMAP
		+DROPOFF
		+CANNOTPUSH
		+SKYEXPLODE
		+NOBLOCKMONST
		+NOSKIN
	}
	States
	{
	Spawn:
		BSKL A 0;
		BSKL ABCDE 5 A_CheckFloor("Hit");
		Goto Spawn+1;
	Hit:
		BSKL F 16 A_CheckPlayerDone;
		Wait;
	}
}

// The mod's Ice Arrow calls it; it lived in Strife's shared action functions. Verbatim.
extend class Actor
{
	void A_AlertMonsters(double maxdist = 0, int flags = 0)
	{
		Actor target = null;
		Actor emitter = self;

		if (player != null || (Flags & AMF_TARGETEMITTER))
		{
			target = self;
		}
		else if (self.target != null && (self.target.player != null || (Flags & AMF_TARGETNONPLAYER)))
		{
			target = self.target;
		}

		if (Flags & AMF_EMITFROMTARGET) emitter = target;

		if (target != null && emitter != null)
		{
			emitter.SoundAlert(target, false, maxdist);
		}
	}
}

// Empty stand-ins: kept ZScript looks these up by name (FindInventory / GetDefaultByType) and the
// compiler wants the class to exist. Nothing in this game spawns or gives them.
class ArtiFly : Inventory {}
class ArtiPoisonBag1 : Inventory {}
class ArtiPoisonBag2 : Inventory {}
class ArtiPoisonBag3 : Inventory {}
class Coin : Inventory {}
