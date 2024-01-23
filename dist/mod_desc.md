This is a simple SKSE mod allowing you to turn any hand-equipped spell into a shout, including custom spells from other mods.

The goal of this mod is to provide a way to cast spells while leaving your hands free.

This mod is extremely similar to [Cast Spells As Lesser Powers](https://www.nexusmods.com/skyrimspecialedition/mods/65398) in terms of features. The main notable difference is that that mod was implemented in Papyrus, whereas this mod ships as a SKSE DLL. If you're already using Cast Spells As Lesser Powers and are happy with it, there's probably no need to switch.


## **Features**

- Spell power, magicka cost, and XP work as you'd expect.
- Animations and sounds.
- Easy to use.
- Safe to install/uninstall mid-playthrough.


## **Getting Started**

To convert a spell to a shout: Equip the spell to your **right hand**, then press [b][color=#f6b26b]Shift[/color][/b] + [b][color=#f6b26b]=[/color][/b].

To delete a spell shout: Equip the shout, then press [b][color=#f6b26b]Shift[/color][/b] + [b][color=#f6b26b]-[/color][/b].

(Key bindings can be changed in **`EquipSpellsAsShouts.json`**. Gamepad is supported.)

Additional usage details:
- You can have up to 30 spell shouts.
- Bound weapon spells: Cast the level 1 shout to equip to your right hand, and cast either the level 2 or 3 shout to equip to your left.
- By default, ritual spells cannot be converted to shouts (the lack of startup delay makes them kind of OP), but you can enable this feature in **`EquipSpellsAsShouts.json`**.

Video demo (make sure to enable captions):
[youtube]iXzpKBBuavs[/youtube]


## **Other Notes**

- When you're in thirdperson and sheathed, aimed shouts may be off from your crosshair. Unsheathing or switching to firstperson should fix this.
- Spell shouts are subject to cooldowns from other shouts (since spell shouts are bona fide shouts).
- This mod does not support simulating dual casting.


## **Troubleshooting**

If things don't work as expected, check logs for anything suspicious.
1. In **`EquipSpellsAsShouts.json`**, set "log_level" to "trace".
1. Launch Skyrim, replicate your issue.
1. Inspect the contents of **`Documents/My Games/Skyrim Special Edition/SKSE/EquipSpellsAsShouts.log`**


## **Source**

https://github.com/panic-sell/equip-spells-as-shouts
