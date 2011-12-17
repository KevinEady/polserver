/*
History
=======
2005/01/24 Shinigami: added getspyonclient2 to support packet 0xd9 (Spy on Client 2)
2005/08/29 Shinigami: character.spyonclient2 renamed to character.clientinfo
                      getspyonclient2 renamed to getclientinfo
2007/07/09 Shinigami: added isUOKR [bool] - UO:KR client used?
2009/07/20 MuadDib:   Added statement to bypass cryptseed at login. Handled by changing default client recv_state using ssopt flag.
2009/07/23 MuadDib:   updates for new Enum::Packet Out ID
2009/08/25 Shinigami: STLport-5.2.1 fix: init order changed of aosresist
                      STLport-5.2.1 fix: params in call of Log2()
2009/09/06 Turley:    Added u8 ClientType + FlagEnum
                      Removed is*
2010/01/22 Turley:    Speedhack Prevention System

Notes
=======

*/

#include "../../clib/stl_inc.h"

#include "../../clib/fdump.h"
#include "../../clib/logfile.h"
#include "../../clib/stlutil.h"
#include "../../clib/strutil.h" //CNXBUG
#include "../../clib/unicode.h"

#include "../../bscript/berror.h"

#include "../accounts/account.h"
#include "cgdata.h"
#include "../mobile/charactr.h"
#include "client.h"
#include "cliface.h"
#include "../crypt/cryptengine.h"
#include "../msgfiltr.h"
#include "../pktin.h"
#include "../polcfg.h"
#include "../polsig.h"
#include "../polstats.h"
#include "../sockio.h"
#include "../ssopt.h"
#include "../module/unimod.h"
#include "../uoclient.h"
#include "../uvars.h"
#include "../uworld.h"
#include "../xbuffer.h"

// only in here temporarily, until logout-on-disconnect stuff is removed
#include "../ufunc.h"

unsigned int Client::instance_counter_;

Client::Client( ClientInterface& aInterface, TCryptInfo& encryption ) :
	acct(NULL),
	chr(NULL),
    Interface(aInterface),
	ready(false),
	csocket(INVALID_SOCKET),
    listen_port(0),
	aosresist(false),
	disconnect(0),
	preDisconnect(0),
	recv_state( RECV_STATE_CRYPTSEED_WAIT ),
    bufcheck1_AA(0xAA),
    buffer(),
    bufcheck2_55(0x55),
	bytes_received(0),
	message_length(0),
    cryptengine( create_crypt_engine( encryption ) ),
	encrypt_server_stream(0),
	msgtype_filter(&login_filter),
    fpLog(NULL),
	pause_count(0),
	first_xmit_buffer(NULL),
	last_xmit_buffer(NULL),
	n_queued(0),
    queued_bytes_counter(0),
    gd(new ClientGameData),
    instance_( ++instance_counter_ ),
    checkpoint(-1), //CNXBUG
    last_msgtype(255),
    thread_pid(-1),
    UOExpansionFlag(0),
	UOExpansionFlagClient(0),
	ClientType(0),
	next_movement(0),
	movementsequence(0),
    paused_(false)
{
	// For bypassing cryptseed packet
	if ( ssopt.use_edit_server )
	{
		recv_state = RECV_STATE_MSGTYPE_WAIT;
	}

    Interface.register_client( this );
	memset( buffer, 0, sizeof buffer );
    memset( &counters, 0, sizeof counters );
    memset( &clientinfo_, 0, sizeof(clientinfo_) );
	memset( &versiondetail_, 0, sizeof(versiondetail_) );
	this->add_ref(); //NOTE: since Client is ref_counted +1 so escript cannot delete class
}

void cancel_trade( Character* chr1 );

void Client::Delete( Client* client )
{
    client->PreDelete();
    delete client;
}

Client::~Client()
{
}

void Client::closeConnection()
{
    Interface.deregister_client( this );
	LocalMutex guard(&_SocketMutex);
	if (csocket != INVALID_SOCKET)//>= 0)
	{
#ifdef _WIN32
        shutdown( csocket,2 ); //2 is both sides, defined in winsock2.h ...
		closesocket( csocket );
#else
        shutdown( csocket,SHUT_RDWR );
		close( csocket );
#endif
	}
	csocket = INVALID_SOCKET;
}

void Client::PreDelete()
{
    closeConnection();
    // FIXME: TEMPORARY FIX: disassociate the character from the account, so
    // you can log on with another character
    if ((acct != NULL) &&
        (acct->active_character != NULL) &&
        (acct->active_character->client == this))
    {
        if (acct->active_character->logged_in)
        {
            Character* tchr = acct->active_character;
            ClrCharacterWorldPosition( tchr, "Client Exit" );
            send_remove_character_to_nearby( tchr );
            tchr->logged_in = false;

            tchr->set_opponent( NULL );
            tchr->removal_cleanup();
			if (tchr->get_opponent() != NULL)
			{
				tchr->set_opponent(NULL, true);
			}
        }
        else
        {
            cerr << "Uhh...  active character not logged in!??" << endl;
        }
        acct->active_character = NULL;
    }

	// detach the account and character from this client, if they
	// are still associated with it.

	acct = NULL;

	if (chr)
    {
        if (chr->client == this)
            chr->client = NULL;
	    chr = NULL;
    }

    if (fpLog != NULL)
    {
        time_t now = time(NULL);
        fprintf( fpLog, "Log closed at %s\n", asctime( localtime( &now ) ) );

        fclose( fpLog );
        fpLog = NULL;
    }

    delete gd;
    gd = NULL;

    while (first_xmit_buffer != NULL)
    {
        XmitBuffer* xbuffer = first_xmit_buffer;
        first_xmit_buffer = first_xmit_buffer->next;
        free(xbuffer);
        --n_queued;
    }
    last_xmit_buffer = NULL;

	//while (!movementqueue.empty())
	//	movementqueue.pop();
}

// ClientInfo - delivers a lot of usefull infomation about client PC
BStruct* Client::getclientinfo() const
{
    auto_ptr<BStruct> ret (new BStruct);

    ret->addMember( "unknown1",          new BLong( clientinfo_.unknown1 ) );          // Unknown - allways 0x02
    ret->addMember( "instance",          new BLong( clientinfo_.instance ) );          // Unique Instance ID of UO
    ret->addMember( "os_major",          new BLong( clientinfo_.os_major ) );          // OS Major
    ret->addMember( "os_minor",          new BLong( clientinfo_.os_minor ) );          // OS Minor
    ret->addMember( "os_revision",       new BLong( clientinfo_.os_revision ) );       // OS Revision
    ret->addMember( "cpu_manufacturer",  new BLong( clientinfo_.cpu_manufacturer ) );  // CPU Manufacturer
    ret->addMember( "cpu_family",        new BLong( clientinfo_.cpu_family ) );        // CPU Family
    ret->addMember( "cpu_model",         new BLong( clientinfo_.cpu_model ) );         // CPU Model
    ret->addMember( "cpu_clockspeed",    new BLong( clientinfo_.cpu_clockspeed ) );    // CPU Clock Speed [Mhz]
    ret->addMember( "cpu_quantity",      new BLong( clientinfo_.cpu_quantity ) );      // CPU Quantity
    ret->addMember( "memory",            new BLong( clientinfo_.memory ) );            // Memory [MB]
    ret->addMember( "screen_width",      new BLong( clientinfo_.screen_width ) );      // Screen Width
    ret->addMember( "screen_height",     new BLong( clientinfo_.screen_height ) );     // Screen Height
    ret->addMember( "screen_depth",      new BLong( clientinfo_.screen_depth ) );      // Screen Depth [Bit]
    ret->addMember( "directx_major",     new BLong( clientinfo_.directx_major ) );     // DirectX Major
    ret->addMember( "directx_minor",     new BLong( clientinfo_.directx_minor ) );     // DirectX Minor

    ObjArray* arr_vd;
    unsigned maxlen_vd = sizeof(clientinfo_.video_description) / sizeof(clientinfo_.video_description[0]);
    unsigned wlen_vd = 0;
    while ( (clientinfo_.video_description[wlen_vd] != L'\0') && (wlen_vd < maxlen_vd) )
      ++wlen_vd;
    if ( !convertUCtoArray(clientinfo_.video_description, arr_vd, wlen_vd, true) )
      ret->addMember( "video_description", new BError("Invalid Unicode speech received.") );
    else {
      ret->addMember( "video_description", arr_vd ); // Video Card Description [wide-character]
    }

    ret->addMember( "video_vendor",      new BLong( clientinfo_.video_vendor ) );      // Video Card Vendor ID
    ret->addMember( "video_device",      new BLong( clientinfo_.video_device ) );      // Video Card Device ID
    ret->addMember( "video_memory",      new BLong( clientinfo_.video_memory ) );      // Video Card Memory [MB]
    ret->addMember( "distribution",      new BLong( clientinfo_.distribution ) );      // Distribution
    ret->addMember( "clients_running",   new BLong( clientinfo_.clients_running ) );   // Clients Running
    ret->addMember( "clients_installed", new BLong( clientinfo_.clients_installed ) ); // Clients Installed
    ret->addMember( "partial_installed", new BLong( clientinfo_.partial_installed ) ); // Partial Insstalled

    ObjArray* arr_lc;
    unsigned maxlen_lc = sizeof(clientinfo_.langcode) / sizeof(clientinfo_.langcode[0]);
    unsigned wlen_lc = 0;
    while ( (clientinfo_.langcode[wlen_lc] != L'\0') && (wlen_lc < maxlen_lc) )
      ++wlen_lc;
    if ( !convertUCtoArray(clientinfo_.langcode, arr_lc, wlen_lc, true) )
      ret->addMember( "langcode", new BError("Invalid Unicode speech received.") );
    else {
      ret->addMember( "langcode", arr_lc ); // Language Code [wide-character]
    }

    auto_ptr<ObjArray> arr_u2 (new ObjArray);
    for ( unsigned i = 0; i < sizeof(clientinfo_.unknown2); ++i )
      arr_u2->addElement( new BLong( clientinfo_.unknown2[i] ) );
	ret->addMember( "unknown2", arr_u2.release() ); // Unknown

	return ret.release();
}

void Client::itemizeclientversion(const std::string& ver, VersionDetailStruct& detail)
{
	try
	{
		int dot1 = ver.find_first_of('.',0);
		int dot2 = ver.find_first_of('.',dot1 + 1);
		int dot3 = ver.find_first_of('.',dot2 + 1);
		if (dot3 == -1)  // since 5.0.7 patch is digit
		{
			dot3 = dot2 + 1;
			while ( (dot3 < (int)ver.length()) && (isdigit(ver[dot3])) )
			{
				dot3++;
			}
		}

		detail.major = atoi(ver.substr(0,dot1).c_str());
		detail.minor = atoi(ver.substr(dot1+1,dot2 - dot1 - 1).c_str());
		detail.rev   = atoi(ver.substr(dot2+1,dot3 - dot2 - 1).c_str());
		detail.patch = 0;
		if (dot3<(int)ver.length())
		{
			if ( (detail.major<=5) && (detail.minor<=0) && (detail.rev<=6))
			{
				if (ver[dot3]!=' ')
					detail.patch = (ver[dot3] - 'a') + 1;  // char to int
			}
			else
				detail.patch = atoi(ver.substr(dot3+1,ver.length() - dot3 - 1).c_str());
		}
	}
	catch(...)
	{
		detail.major = 0;
		detail.minor = 0;
		detail.rev   = 0;
		detail.patch = 0;
		Log2("Malformed clientversion string: %s\n",ver.c_str());
	}
}

bool Client::compareVersion( const std::string& ver )
{
	VersionDetailStruct ver2;
	itemizeclientversion(ver, ver2);
	return Client::compareVersion(ver2);
}

bool Client::compareVersion(const VersionDetailStruct& ver2)
{
	VersionDetailStruct ver1 = getversiondetail();

	if      ( ver1.major > ver2.major )
		return true;
	else if ( ver1.major < ver2.major )
		return false;
	else if ( ver1.minor > ver2.minor )
		return true;
	else if ( ver1.minor < ver2.minor )
		return false;
	else if ( ver1.rev   > ver2.rev )
		return true;
	else if ( ver1.rev   < ver2.rev )
		return false;
	else if ( ver1.patch > ver2.patch )
		return true;
	else if ( ver1.patch < ver2.patch )
		return false;
	else
		return true;
}

void Client::setClientType(ClientTypeFlag type)
{
	ClientType = 0x0;
	// with fall through !
	switch (type)
	{
	case CLIENTTYPE_70130:
		ClientType |= CLIENTTYPE_70130;
	case CLIENTTYPE_7090:
		ClientType |= CLIENTTYPE_7090;
	case CLIENTTYPE_UOSA:
		ClientType |= CLIENTTYPE_UOSA;
    case CLIENTTYPE_7000:
        ClientType |= CLIENTTYPE_7000;
	case CLIENTTYPE_UOKR:
		ClientType |= CLIENTTYPE_UOKR;
	case CLIENTTYPE_60142:
		ClientType |= CLIENTTYPE_60142;
	case CLIENTTYPE_6017:
		ClientType |= CLIENTTYPE_6017;
    case CLIENTTYPE_5020:
        ClientType |= CLIENTTYPE_5020;
	case CLIENTTYPE_5000:
		ClientType |= CLIENTTYPE_5000;
    case CLIENTTYPE_4070:
        ClientType |= CLIENTTYPE_4070;
    case CLIENTTYPE_4000:
        ClientType |= CLIENTTYPE_4000;
	default:
		break;
	}
}

bool Client::IsUOKRClient()
{
	return ((ClientType & CLIENTTYPE_UOKR) && (!(ClientType & CLIENTTYPE_7000)));
}

std::string Client::status() const
{
    std::string st;
    if (acct != NULL)
        st += "AC:" + std::string(acct->name()) + " ";
    if (chr != NULL)
        st += "CH:" + chr->name() + " ";
    if (have_queued_data())
        st += "TXBUF ";
    if (disconnect)
        st += "DISC ";
    if (paused_)
        st += "PAUSE ";
    if (ready)
        st += "RDY ";
    st += ipaddrAsString() + " ";
    st += "CHK: " + decint(checkpoint) + " ";
    st += "PID: " + decint(thread_pid) + " ";
    st += "LAST: " + hexint(last_msgtype);
    return st;
}

void Client::queue_data( const void *data, unsigned short datalen )
{
    THREAD_CHECKPOINT( active_client, 300 );
	XmitBuffer *xbuffer = (XmitBuffer *) malloc( sizeof(XmitBuffer)-1+datalen );
    THREAD_CHECKPOINT( active_client, 301 );
    if (xbuffer)
    {
        THREAD_CHECKPOINT( active_client, 302 );
	    xbuffer->next = NULL;
	    xbuffer->nsent = 0;
	    xbuffer->lenleft = datalen;
	    memcpy( xbuffer->data, data, datalen );
        THREAD_CHECKPOINT( active_client, 303 );
	    if (first_xmit_buffer == NULL)
	    {	// in this case, last_xmit_buffer is also NULL, so can't set its ->next.
            THREAD_CHECKPOINT( active_client, 304 );
		    first_xmit_buffer = xbuffer;
	    }
	    else
	    {
            THREAD_CHECKPOINT( active_client, 305 );
		    last_xmit_buffer->next = xbuffer;
	    }
        THREAD_CHECKPOINT( active_client, 306 );
	    last_xmit_buffer = xbuffer;
	    ++n_queued;
        queued_bytes_counter += datalen;
    }
    else
    {
        THREAD_CHECKPOINT( active_client, 307 );
        Log( "Client#%lu: Unable to allocate %d bytes for queued data.  Disconnecting.\n",
                instance_, sizeof(XmitBuffer)-1+datalen );
        disconnect = true;
    }
    THREAD_CHECKPOINT( active_client, 309 );

}

void Client::xmit( const void *data, unsigned short datalen )
{
	if(encrypt_server_stream)
		this->cryptengine->Encrypt((void *)data, (void *)data, datalen);
    THREAD_CHECKPOINT( active_client, 200 );
	if (last_xmit_buffer) // this client already backlogged, schedule for later
	{
        THREAD_CHECKPOINT( active_client, 201 );
		queue_data( data, datalen );
        THREAD_CHECKPOINT( active_client, 202 );
        return;
	}
    THREAD_CHECKPOINT( active_client, 203 );

	/* client not backlogged - try to send. */
	const unsigned char *cdata = (const unsigned char *) data;
	int nsent;

	if (-1 == (nsent = send( csocket, (const char *)cdata, datalen, 0 )))
	{
        THREAD_CHECKPOINT( active_client, 204 );
        int sckerr = socket_errno;

        if (sckerr == SOCKET_ERRNO(EWOULDBLOCK))
		{
            THREAD_CHECKPOINT( active_client, 205 );
            Log( "Client#%lu: Switching to queued data mode (1, %u bytes)\n", instance_, (unsigned) datalen );
            cerr << "Switching to queued data mode (1, " << datalen << " bytes)" << endl;
            THREAD_CHECKPOINT( active_client, 206 );
			queue_data( data, datalen );
            THREAD_CHECKPOINT( active_client, 207 );
			return;
		}
		else
		{
            THREAD_CHECKPOINT( active_client, 208 );
            if (!disconnect)
                Log( "Client#%lu: Disconnecting client due to send() error (1): %d\n" , instance_, sckerr);
			disconnect = 1;
            THREAD_CHECKPOINT( active_client, 209 );
			return;
		}
	}
	else // no error
	{
        THREAD_CHECKPOINT( active_client, 210 );
		datalen -= static_cast<unsigned short>(nsent);
        counters.bytes_transmitted += nsent;
        polstats.bytes_sent += nsent;
		if (datalen)	// anything left? if so, queue for later.
		{
            THREAD_CHECKPOINT( active_client, 211 );
            Log( "Client#%lu: Switching to queued data mode (2)\n", instance_ );
            cerr << "Switching to queued data mode (2)" << endl;
            THREAD_CHECKPOINT( active_client, 212 );
			queue_data( cdata+nsent, datalen );
            THREAD_CHECKPOINT( active_client, 213 );
		}
	}
    THREAD_CHECKPOINT( active_client, 214 );
}

void Client::send_queued_data()
{
	LocalMutex guard(&_SocketMutex);
	XmitBuffer *xbuffer;
	// hand off data to the sockets layer until it won't take any more.
	// note if a buffer is sent in full, we try to send the next one, ad infinitum
	while (NULL != (xbuffer = first_xmit_buffer))
	{
		int nsent;
		nsent = send( csocket,
			          (char *) &xbuffer->data[xbuffer->nsent],
					  xbuffer->lenleft,
					  0 );
		if (nsent == -1)
		{
#ifdef _WIN32
            int sckerr = WSAGetLastError();
#else
            int sckerr = errno;
#endif
			if (sckerr == SOCKET_ERRNO(EWOULDBLOCK))
			{
				// do nothing.  it'll be re-queued later, when it won't block.
				return;
			}
			else
			{
                if (!disconnect)
                    Log( "Client#%lu: Disconnecting client due to send() error (2): %d\n", instance_, sckerr );
				disconnect = 1;
				return;
			}
		}
		else
		{
			xbuffer->nsent += static_cast<unsigned short>(nsent);
			xbuffer->lenleft -= static_cast<unsigned short>(nsent);
            counters.bytes_transmitted += nsent;
            polstats.bytes_sent += nsent;
			if (xbuffer->lenleft == 0)
			{
				first_xmit_buffer = first_xmit_buffer->next;
				if (first_xmit_buffer == NULL)
                {
					last_xmit_buffer = NULL;
                    Log( "Client#%lu: Leaving queued mode (%ld bytes xmitted)\n", instance_, queued_bytes_counter );
                    queued_bytes_counter = 0;
                }
				free( xbuffer );
				--n_queued;
			}
		}
	}
}

#define PRE_ENCRYPT

#ifndef PRE_ENCRYPT
#include "sockio.h"
#endif

// 33 01 "encrypted": 4F FA
static const unsigned char pause_pre_encrypted[2] = { 0x4F, 0xFA };
// 33 00 "encrypted": 4C D0
static const unsigned char restart_pre_encrypted[2] = { 0x4C, 0xD0 };

void Client::send_pause(bool bForce /*=false*/)
{
    if ((bForce || uoclient_protocol.EnableFlowControlPackets) && !paused_)
    {
#ifndef PRE_ENCRYPT
		PKTOUT_33 msg;
		msg.msgtype = PKTOUT_33_ID;
		msg.flow = MSGOPT_33_FLOW_PAUSE;
		transmit( this, &msg, sizeof msg );
#else
		xmit( pause_pre_encrypted, sizeof pause_pre_encrypted );
#endif
        paused_ = true;
        // cout << "Client#" << instance_ << " paused" << endl;
	}
}

void Client::pause()
{
	if (!pause_count)
	{
        send_pause();
        pause_count = 1;
	}
	// ++pause_count;
}

void Client::send_restart(bool bForce /*=false*/)
{
    if (paused_)
    {
#ifndef PRE_ENCRYPT
		PKTOUT_33 msg;
		msg.msgtype = PKTOUT_33_ID;
		msg.flow = MSGOPT_33_FLOW_RESTART;
		transmit( this, &msg, sizeof msg );
#else
		xmit( restart_pre_encrypted, sizeof restart_pre_encrypted );
#endif
        // cout << "Client#" << instance_ << " restarted" << endl;
        paused_ = false;
	}
}

// FIXME: Why is this empty??
void Client::restart()
{
}

void Client::restart2()
{
    send_restart();
    pause_count = 0;
}

// Note: this doesnt test single packets it only summs the delay and tests
// here only the "start"-value is set the additional delay is set in PKT_02 handler
bool Client::SpeedHackPrevention(bool add)
{
	if ((!movementqueue.empty()) && (add))
	{
		if (movementqueue.size()>100)
		{
			Log2( "Client#%lu: More then 100 Movepackets in queue.  Disconnecting.\n",
				instance_ );
			disconnect = true;
			return false;
		}
		PacketThrottler throttlestruct;
		memcpy(throttlestruct.pktbuffer, buffer, PKTIN_02_SIZE);
		movementqueue.push(throttlestruct);
		return false;
	}
	if (chr->can_speedhack())
		return true;
	if ((next_movement == 0) || (wallclock() > next_movement)) // never moved or in the past
	{
		next_movement = wallclock();
		return true;
	}
	// now we dont alter next_movement so we can sum the delay till diff is greater then error margin
	wallclock_diff_t diff = wallclock_diff_ms(wallclock(), next_movement);
	if (diff > PKTIN_02_ASYNCHRONOUS) // delay sum greater then our error margin?
	{
		if (add) // delay packet
		{
			if (movementqueue.size()>100)
			{
				Log2( "Client#%lu: More then 100 Movepackets in queue.  Disconnecting.\n",
					instance_ );
				disconnect = true;
				return false;
			}
			PacketThrottler throttlestruct;
			memcpy(throttlestruct.pktbuffer, buffer, PKTIN_02_SIZE);
			movementqueue.push(throttlestruct);
		}
		return false;
	}
	return true;
}

BObjectImp* Client::make_ref()
{
	return new EClientRefObjImp( ClientPtrHolder(ClientRef(this)) );
}

