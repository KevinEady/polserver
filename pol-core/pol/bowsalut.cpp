/*
History
=======
2005/11/26 Shinigami: changed "strcmp" into "stricmp" to suppress Script Errors
2009/07/27 MuadDib:   Pakcet struct refactoring

Notes
=======

*/

#include "clib/stl_inc.h"

#include "clib/cfgelem.h"
#include "clib/cfgfile.h"
#include "clib/endian.h"
#include "clib/fileutil.h"
#include "clib/strutil.h"

#include "charactr.h"
#include "client.h"
#include "extcmd.h"
#include "pktin.h"
#include "uobject.h"
#include "uvars.h"
#include "ufunc.h"
#include "sockio.h"

#include "action.h"

UACTION mount_action_xlate[ ACTION__HIGHEST+1 ];

UACTION str_to_action( ConfigElem& elem, const string& str )
{
    unsigned short tmp = static_cast<unsigned short>(strtoul( str.c_str(), NULL, 0 ));

    if (UACTION_IS_VALID( tmp ))
    {
        return static_cast<UACTION>(tmp);
    }
    else
    {
        elem.throw_error( "Animation value of " + str + " is out of range" );
        return ACTION__LOWEST;
    }
}

void load_anim_xlate_cfg( bool reload )
{
    memset( mount_action_xlate, 0, sizeof mount_action_xlate );

    if (FileExists( "config/animxlate.cfg" ))
    {
        ConfigFile cf( "config/animxlate.cfg", "OnMount" );
        ConfigElem elem;

        if (cf.read( elem ))
        {
            string from_str, to_str;
            while (elem.remove_first_prop( &from_str, &to_str ))
            {
                UACTION from = str_to_action( elem, from_str );
                UACTION to = str_to_action( elem, to_str );
                mount_action_xlate[ from ] = to;
            }
        }
    }
}

void send_action_to_inrange( const Character* obj, UACTION action,
							unsigned short framecount /*=0x05*/,
							unsigned short repeatcount /*=0x01*/, 
							PKTOUT_6E::DIRECTION_FLAG backward /*=PKTOUT_6E::FORWARD*/, 
							PKTOUT_6E::REPEAT_FLAG repeatflag /*=PKTOUT_6E::NOREPEAT*/, 
							unsigned char delay /*=0x01*/ )
{
    if (obj->on_mount())
    {
        if (action < ACTION_RIDINGHORSE1 || action > ACTION_RIDINGHORSE7)
        {
            UACTION new_action = mount_action_xlate[ action ];
            if (new_action == 0)
                return;
            action = new_action;
        }
    }

	PKTOUT_6E msg;
	msg.msgtype = PKTOUT_6E_ID;
	msg.serial = obj->serial_ext;
	msg.action = ctBEu16( (u16)action );
	msg.framecount = ctBEu16(framecount);
	msg.repeatcount = ctBEu16(repeatcount);
	msg.backward = static_cast<u8>(backward);
	msg.repeatflag = static_cast<u8>(repeatflag);
	msg.delay = delay;

	transmit_to_inrange( obj, &msg, sizeof msg, false, false );
}

void handle_action( Client *client, PKTIN_12 *cmd )
{
	if (stricmp( (const char *) cmd->data, "bow" ) == 0)
		send_action_to_inrange( client->chr, ACTION_BOW );
	else if (stricmp( (const char *) cmd->data, "salute" ) == 0)
		send_action_to_inrange( client->chr, ACTION_SALUTE );
}

ExtendedMessageHandler action_handler( EXTMSGID_ACTION, handle_action );