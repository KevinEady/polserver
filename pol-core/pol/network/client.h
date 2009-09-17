/*
History
=======
2005/01/24 Shinigami: added get-/setspyonclient2 to support packet 0xd9 (Spy on Client 2)
2005/04/03 Shinigami: added UOExpansionFlag for Samurai Empire
2005/08/29 Shinigami: character.spyonclient2 renamed to character.clientinfo
                      get-/setspyonclient2 renamed to get-/setclientinfo
2006/05/16 Shinigami: added UOExpansionFlag for Mondain's Legacy
                      added GENDER/RACE flag (e.g. used inside ClientCreateChar())
2007/07/09 Shinigami: added isUOKR [bool] - UO:KR client used?
2009/08/10 MuadDib:   Added CLIENT_VER_50000 for v5.0.0x clients.
2009/08/19 Turley:    Added u32 UOExpansionFlagClient
2009/09/06 Turley:    Added u8 ClientType + FlagEnum
                      Removed is*

Notes
=======

*/

#ifndef __CLIENT_H
#define __CLIENT_H

#include <stdio.h> // for that FILE fpLog down there :(
#include <memory>
#include <string>

#include "../../clib/rawtypes.h"

#include "../../bscript/bstruct.h"

#include "../pktin.h"
#include "../sockets.h"
#include "../ucfg.h"
#include "../ucrypto.h"
#include "../crypt/cryptengine.h"

class MessageTypeFilter;
class Account;
class Character;
class UContainer;
struct XmitBuffer;
class ClientGameData;
class ClientInterface;
class UOClientInterface;

extern UOClientInterface uo_client_interface;

const u16 T2A = 0x01;
const u16 LBR = 0x02;
const u16 AOS = 0x04;
const u16 SE  = 0x08; // set AOS-Flag in send_feature_enable() too for needed checks
const u16 ML  = 0x10; // set SE- and AOS-Flag in send_feature_enable() too for needed checks
const u16 KR  = 0x20; // set KR- and SE- and AOS-Flag in send_feature_enable() too for needed checks

const u8 FLAG_GENDER = 0x01;
const u8 FLAG_RACE   = 0x02;


struct VersionDetailStruct
{
	int major;
    int minor;
	int rev;
	int patch;
};

const struct VersionDetailStruct CLIENT_VER_50000={5,0,0,0};
const struct VersionDetailStruct CLIENT_VER_60171={6,0,1,7};
const struct VersionDetailStruct CLIENT_VER_60142={6,0,14,2};
const struct VersionDetailStruct CLIENT_VER_70000={7,0,0,0};

enum ClientTypeFlag
{
	CLIENTTYPE_5000  = 0x1,
	CLIENTTYPE_6017  = 0x2,
	CLIENTTYPE_60142 = 0x4,
	CLIENTTYPE_UOKR  = 0x8,
	CLIENTTYPE_UOSA  = 0x10
};

class Client
{
public:
	Client( ClientInterface& aInterface, const string& encryption );
    static void Delete( Client* client );
    friend class GCCHelper;

private:
    void PreDelete();
    virtual ~Client();

public:
    void Disconnect();
    void transmit( const void *data, int len ); // for entire message or header only
    void transmitmore( const void *data, int len ); // for stuff after a header

    void recv_remaining( int total_expected );
    void recv_remaining_nocrypt( int total_expected );

    void setversion( const std::string& ver ) { version_ = ver; }
    const std::string& getversion() const { return version_; }
	VersionDetailStruct getversiondetail() const { return versiondetail_; }
	void setversiondetail( VersionDetailStruct& detail ) { versiondetail_ = detail; }
	void itemizeclientversion( const std::string& ver, VersionDetailStruct& detail );
	bool compareVersion( const std::string& ver );
	bool compareVersion(const VersionDetailStruct& ver2);
	void setClientType(ClientTypeFlag type);

    void setclientinfo( const PKTIN_D9 *msg ) { memcpy( &clientinfo_, msg, sizeof(clientinfo_) ); }
    BStruct* getclientinfo() const;

    Account* acct;
	Character* chr;
    ClientInterface& Interface;

	bool ready;			// all initialization stuff has been sent, ready for general use.


//
    bool have_queued_data() const;
	void send_queued_data();

	SOCKET csocket;		// socket to client ACK  - requires header inclusion.
    unsigned short listen_port;
	bool aosresist; // UOClient.Cfg Entry

	bool disconnect;		// if 1, disconnect this client

	enum e_recv_states {
			RECV_STATE_CRYPTSEED_WAIT,
			RECV_STATE_MSGTYPE_WAIT,
			RECV_STATE_MSGLEN_WAIT,
			RECV_STATE_MSGDATA_WAIT
	} recv_state;

	unsigned char encrypted_data[ MAXBUFFER ];
    unsigned char bufcheck1_AA;
	unsigned char buffer[ MAXBUFFER ];
    unsigned char bufcheck2_55;
	unsigned int bytes_received;		// how many bytes have been received into the buffer.
	unsigned int message_length;		// how many bytes are expected for this message

	struct sockaddr ipaddr;

	ClientEncryptionEngine clicrypt;

    std::auto_ptr<CryptEngine> cryptengine;

    //CCrypt newcrypt;
    unsigned char cryptseed[4];

    void setcryptseed( unsigned char cryptseed[4] );

	bool encrypt_server_stream;				// encrypt the server stream (data sent to client)?

	const MessageTypeFilter *msgtype_filter;

    FILE* fpLog;

    std::string status() const;

    void send_pause(bool bForce = false);
    void send_restart(bool bForce = false);

    void pause();
	void restart();
    void restart2();
	int pause_count;

    std::string ipaddrAsString() const;

protected:

	XmitBuffer *first_xmit_buffer;
	XmitBuffer *last_xmit_buffer;
	long n_queued;
    long queued_bytes_counter; // only used for monitoring

	// we may want to track how many bytes total are outstanding,
	// and boot clients that are too far behind.
	void queue_data( const void *data, unsigned short datalen );
    void transmit_encrypted( const void *data, int len );
	void xmit( const void *data, unsigned short datalen );

public:
    ClientGameData* gd;
    unsigned long instance_;
    static unsigned long instance_counter_;
    int checkpoint;//CNXBUG
    unsigned char last_msgtype;
    int thread_pid;
    u16 UOExpansionFlag;
	u32 UOExpansionFlagClient;
	u8 ClientType;
	
private:
    struct {
        unsigned long bytes_transmitted;
        unsigned long bytes_received;
    } counters;
    std::string version_;
    PKTIN_D9 clientinfo_;
    bool paused_;
    VersionDetailStruct versiondetail_;
    // hidden:
    Client( const Client& x );
    Client& operator=( const Client& x );
};

inline bool Client::have_queued_data() const
{
	return (first_xmit_buffer != NULL);
}

#endif