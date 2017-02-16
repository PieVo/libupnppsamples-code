// Exercise a variety of libupnpp features...

#include <getopt.h>

#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <mutex>
#include <condition_variable>

#include "libupnpp/upnpplib.hxx"
#include "libupnpp/log.hxx"
#include "libupnpp/upnpputils.hxx"
#include "libupnpp/control/service.hxx"
#include "libupnpp/control/cdirectory.hxx"
#include "libupnpp/control/mediarenderer.hxx"
#include "libupnpp/control/renderingcontrol.hxx"
#include "libupnpp/control/discovery.hxx"

using namespace UPnPClient;
using namespace UPnPP;
using namespace std;

UPnPDeviceDirectory *superdir;

std::mutex reporterLock;
std::condition_variable evloopcond;

vector<string> deviceFNs;
vector<string> deviceUDNs;
vector<string> deviceTypes;
static void clearDevices() {
    deviceFNs.clear();
    deviceUDNs.clear();
    deviceTypes.clear();
}

static bool 
reporter(const UPnPDeviceDesc& device, const UPnPServiceDesc&)
{
    std::unique_lock<std::mutex> lock(reporterLock);
    //cerr << "reporter: " << device.friendlyName << " s " << 
    // device.deviceType << endl;
    if (find(deviceUDNs.begin(), deviceUDNs.end(), device.UDN)
        == deviceUDNs.end()) {
        deviceFNs.push_back(device.friendlyName);
        deviceUDNs.push_back(device.UDN);
        deviceTypes.push_back(device.deviceType);
        evloopcond.notify_all();
    }
    return true;
}

static bool traverser(const UPnPDeviceDesc& device, const UPnPServiceDesc& srv)
{
    if (find(deviceUDNs.begin(), deviceUDNs.end(), device.UDN)
        == deviceUDNs.end()) {
        cout << device.friendlyName <<" ("<< device.deviceType << ")" << endl;
        deviceFNs.push_back(device.friendlyName);
        deviceUDNs.push_back(device.UDN);
        deviceTypes.push_back(device.deviceType);
    }        
    return true;
}

void listDevices()
{
    cout << "UPnP devices:" << endl;
    static int cbindex = -1;
    if (cbindex == -1) {
        cbindex = UPnPDeviceDirectory::addCallback(reporter);
    }
    if (superdir == 0) {
        superdir = UPnPDeviceDirectory::getTheDir(1);
        if (superdir == 0) {
            cerr << "can't get superdir\n";
            exit(1);
        }
    }

    // Until the initial delay is through, use the reporter to list
    // devices as they come
    unsigned int ndevices = 0;
    for (;;) {
        std::unique_lock<std::mutex> lock(reporterLock);
#if FUTURE
        int ms = superdir->getRemainingDelayMs();
#else
        int ms = superdir->getRemainingDelay() * 1000;
#endif
        if (ms > 0) {
            evloopcond.wait_for(lock, std::chrono::milliseconds(ms));
            if (deviceFNs.size() > ndevices) {
                for (unsigned int i = ndevices; i < deviceFNs.size(); i++) {
                    cout << deviceFNs[i] <<" ("<< deviceTypes[i] << ")" << endl;
                }
                ndevices = deviceFNs.size();
            }
        } else {
            break;
        }
    }

    cerr << "Initial delay done\n";

    // Called after initial delay done. Unset the callback and
    // traverse the directory
    if (cbindex >= 0) {
        UPnPDeviceDirectory::delCallback(cbindex);
        cbindex = -2;
    }
    clearDevices();
    auto ret = superdir->traverse(traverser);
    cerr << "Now having " << deviceUDNs.size() << " devices " << endl;
}

void listServers()
{
    cout << "Content Directories:" << endl;
    vector<CDSH> dirservices;
    if (!ContentDirectory::getServices(dirservices)) {
        cerr << "listDirServices failed" << endl;
        return;
    }
    for (vector<CDSH>::iterator it = dirservices.begin();
         it != dirservices.end(); it++) {
        cout << (*it)->getFriendlyName() << endl;
    }
    cout << endl;
}

void listPlayers()
{
    cout << "Media Renderers:" << endl;
    vector<UPnPDeviceDesc> vdds;
    if (!MediaRenderer::getDeviceDescs(vdds)) {
        cerr << "MediaRenderer::getDeviceDescs" << endl;
        return;
    }
    for (auto& entry : vdds) {
        cout << entry.friendlyName << endl;
    }
    cout << endl;
}

class MReporter : public UPnPClient::VarEventReporter {
public:
    void changed(const char *nm, int value)
        {
            cout << "Changed: " << nm << " : " << value << endl;
        }
    void changed(const char *nm, const char *value)
        {
            cout << "Changed: " << nm << " : " << value << endl;
        }

    void changed(const char *nm, UPnPDirObject meta)
        {
            string s = meta.dump();
            cout << "Changed: " << nm << " : " << s << endl;
        }

};

MRDH getRenderer(const string& friendlyName)
{
    if (superdir == 0) {
        superdir = UPnPDeviceDirectory::getTheDir();
    }

    UPnPDeviceDesc ddesc;
    if (superdir->getDevByFName(friendlyName, ddesc)) {
        return MRDH(new MediaRenderer(ddesc));
    }
    cerr << "getDevByFname failed" << endl;
    return MRDH();
}

void getsetVolume(const string& friendlyName, int volume = -1)
{
    MRDH rdr = getRenderer(friendlyName);
    if (!rdr) {
        return;
    }

    RDCH rdc = rdr->rdc();
    if (!rdc) {
        cerr << "Device has no RenderingControl service" << endl;
        return;
    }

    if (volume == -1) {
        volume = rdc->getVolume();
        cout << "Current volume: " << volume << endl;
        return;
    } else {
        if ((volume = rdc->setVolume(volume)) != 0) {
            cerr << "Error setting volume: " << volume << endl;
            return;
        }
    }
}

void tpMonitor(const string& friendlyName)
{
    MRDH rdr = getRenderer(friendlyName);
    if (!rdr) {
        return;
    }
    AVTH avt = rdr->avt();
    if (!avt) {
        cerr << "Device has no AVTransport service" << endl;
        return;
    }
    MReporter reporter;
    avt->installReporter(&reporter);

    while (true) {
        AVTransport::PositionInfo info;
		int ret;
        if ((ret = avt->getPositionInfo(info))) {
            cerr << "getPositionInfo failed. Code " << ret << endl;
        } else {
            cout << info.trackmeta.m_title << " reltime " << info.reltime 
				 << endl;
        }
        sleep(2);
    }
}

int tpAlbumArt(const string& fname)
{
    MRDH rdr = getRenderer(fname);
    if (!rdr) {
        cerr << "Can't connect to renderer " << fname << endl;
        return 1;
    }

    string uri;

    OHIFH ohinfo = rdr->ohif();
    if (ohinfo) {
        UPnPDirObject dirent;
        if (ohinfo->metatext(&dirent) == 0) {
            uri = dirent.getprop("upnp:albumArtURI");
        } else {
            //cerr << "metatext failed\n";
        }
    }

    if (uri.empty()) {
        AVTH avt = rdr->avt();
        if (avt) {
            AVTransport::TransportInfo tinfo;
            int ret;
            if ((ret = avt->getTransportInfo(tinfo))) {
                cerr << "getTransportInfo failed. Code " << ret << endl;
            } else if (tinfo.tpstatus == AVTransport::TPS_Ok && 
                       (tinfo.tpstate == AVTransport::Playing || 
                        tinfo.tpstate == AVTransport::PausedPlayback)) {
                AVTransport::PositionInfo info;
                if ((ret = avt->getPositionInfo(info))) {
                    cerr << "getPositionInfo failed. Code " << ret << endl;
                } else {
                    uri = info.trackmeta.getprop("upnp:albumArtURI");
                }
            }
        }
    }

    cout << uri << endl;
    return 0;
}

void tpPlayStop(const string& friendlyName, bool doplay)
{
    MRDH rdr = getRenderer(friendlyName);
    if (!rdr) {
        return;
    }
    AVTH avt = rdr->avt();
    if (!avt) {
        cerr << "Device has no AVTransport service" << endl;
        return;
    }
    int ret;
    if (doplay) {
        ret = avt->play();
    } else {
        ret = avt->stop();
    }
    if (ret != 0) {
        cerr << "Operation failed: code: " << ret << endl;
    }
}

void tpPause(const string& friendlyName)
{
    MRDH rdr = getRenderer(friendlyName);
    if (!rdr) {
        return;
    }
    AVTH avt = rdr->avt();
    if (!avt) {
        cerr << "Device has no AVTransport service" << endl;
        return;
    }

    avt->pause();
}

void readdir(const string& friendlyName, const string& cid)
{
    cout << "readdir: [" << friendlyName << "] [" << cid << "]" << endl;
    CDSH server;
    if (!ContentDirectory::getServerByName(friendlyName, server)) {
        cerr << "Server not found" << endl;
        return;
    }
    UPnPDirContent dirbuf;
    int code = server->readDir(cid, dirbuf);
    if (code) {
        cerr << LibUPnP::errAsString("readdir", code) << endl;
        return;
    }
    cout << "Browse: got " << dirbuf.m_containers.size() <<
        " containers and " << dirbuf.m_items.size() << " items " << endl;
    for (unsigned int i = 0; i < dirbuf.m_containers.size(); i++) {
        cout << dirbuf.m_containers[i].dump();
    }
    for (unsigned int i = 0; i < dirbuf.m_items.size(); i++) {
        cout << dirbuf.m_items[i].dump();
    }
}

void getMetadata(const string& friendlyName, const string& cid)
{
    cout << "getMeta: [" << friendlyName << "] [" << cid << "]" << endl;
    CDSH server;
    if (!ContentDirectory::getServerByName(friendlyName, server)) {
        cerr << "Server not found" << endl;
        return;
    }
    UPnPDirContent dirbuf;
    int code = server->getMetadata(cid, dirbuf);
    if (code) {
        cerr << LibUPnP::errAsString("readdir", code) << endl;
        return;
    }
    cout << "getMeta: got " << dirbuf.m_containers.size() <<
        " containers and " << dirbuf.m_items.size() << " items " << endl;
    for (unsigned int i = 0; i < dirbuf.m_containers.size(); i++) {
        cout << dirbuf.m_containers[i].dump();
    }
    for (unsigned int i = 0; i < dirbuf.m_items.size(); i++) {
        cout << dirbuf.m_items[i].dump();
    }
}

void search(const string& friendlyName, const string& ss)
{
    cout << "search: [" << friendlyName << "] [" << ss << "]" << endl;
    CDSH server;
    if (!ContentDirectory::getServerByName(friendlyName, server)) {
        cerr << "Server not found" << endl;
        return;
    }
    UPnPDirContent dirbuf;
    string cid("0");
    int code = server->search(cid, ss, dirbuf);
    if (code) {
        cerr << LibUPnP::errAsString("search", code) << endl;
        return;
    }
    cout << "Search: got " << dirbuf.m_containers.size() <<
        " containers and " << dirbuf.m_items.size() << " items " << endl;
    for (unsigned int i = 0; i < dirbuf.m_containers.size(); i++) {
        cout << dirbuf.m_containers[i].dump();
    }
    for (unsigned int i = 0; i < dirbuf.m_items.size(); i++) {
        cout << dirbuf.m_items[i].dump();
    }
}

void getSearchCaps(const string& friendlyName)
{
    cout << "getSearchCaps: [" << friendlyName << "]" << endl;
    CDSH server;
    if (!ContentDirectory::getServerByName(friendlyName, server)) {
        cerr << "Server not found" << endl;
        return;
    }
    set<string> capa;
    int code = server->getSearchCapabilities(capa);
    if (code) {
        cerr << LibUPnP::errAsString("readdir", code) << endl;
        return;
    }
    if (capa.empty()) {
        cout << "No search capabilities";
    } else {
        for (set<string>::const_iterator it = capa.begin();
             it != capa.end(); it++) {
            cout << "[" << *it << "]";
        }
    }
    cout << endl;
}



static char *thisprog;
static char usage [] =
            " -l : list devices\n"
            " -r <server> <objid> list object id (root is '0')\n"
            " -s <server> <searchstring> search for string\n"
            " -m <server> <objid> : list object metadata\n"
            " -c <server> get search capabilities\n"
            " -M <renderer>: monitor AVTransport\n"
            " -v <renderer> get volume\n"
            " -V <renderer> <volume> set volume\n"
            " -p <renderer> 1|0 play/stop\n"
            " -P <renderer>  pause\n"
            " --album-art <renderer> print album art uri for playing track\n"
            "\nFor now all <renderer> and <server> params should be "
            "\"friendly names\", not UUIDs\n"
            " \n"
            ;
static void
Usage(void)
{
    fprintf(stderr, "%s: usage:\n%s", thisprog, usage);
    exit(1);
}
static int	   op_flags;
#define OPT_l    0x1
#define OPT_r    0x2
#define OPT_s    0x4
#define OPT_m    0x8
#define OPT_c    0x10
#define OPT_M    0x20
#define OPT_v    0x40
#define OPT_V    0x80
#define OPT_p    0x100
#define OPT_P    0x200

#define OPT_aa   0x10000

static struct option long_options[] = {
    {"album-art", 0, 0, OPT_aa},
    {0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
    string fname;
    string arg;

    thisprog = argv[0];

    int ret;
    int option_index = 0;
    while ((ret = getopt_long(argc, argv, "MPSVclmprsvx", 
                              long_options, &option_index)) != -1) {
        switch (ret) {
        case 'M': if (op_flags) Usage(); op_flags |= OPT_M; break;
        case 'P': if (op_flags) Usage(); op_flags |= OPT_P; break;
        case 'V': if (op_flags) Usage(); op_flags |= OPT_V; break;
        case 'c': if (op_flags) Usage(); op_flags |= OPT_c; break;
        case 'l': if (op_flags) Usage(); op_flags |= OPT_l; break;
        case 'm': if (op_flags) Usage(); op_flags |= OPT_m; break;
        case 'p': if (op_flags) Usage(); op_flags |= OPT_p; break;
        case 'r': if (op_flags) Usage(); op_flags |= OPT_r; break;
        case 's': if (op_flags) Usage(); op_flags |= OPT_s; break;
        case 'v': if (op_flags) Usage(); op_flags |= OPT_v; break;

        case OPT_aa: if (op_flags) Usage(); op_flags |= OPT_aa; break;

        default:
            Usage();
        }
    }

    if (op_flags & (OPT_l)) {
        if (optind < argc) 
            Usage();
    }

    if (op_flags & (OPT_c | OPT_v | OPT_P | OPT_M | OPT_aa)) {
            if (optind != argc - 1) 
                Usage();
            fname = argv[optind++];
    }
    if (op_flags & (OPT_r | OPT_s | OPT_m | OPT_V | OPT_p)) {
        cerr << "optind " << optind << " argc " << argc << endl;
        if (optind != argc - 2) 
                Usage();
            fname = argv[optind++];
            arg = argv[optind++];
    }

    if (Logger::getTheLog("/tmp/upexplo.log") == 0) {
        cerr << "Can't initialize log" << endl;
        //return 1;
    }
    Logger::getTheLog("")->setLogLevel(Logger::LLDEB1);

    string hwa;
    LibUPnP *mylib = LibUPnP::getLibUPnP(false, &hwa);
    if (!mylib) {
        cerr << "Can't get LibUPnP" << endl;
        return 1;
    }
    //cerr << "hwaddr " << hwa << endl;

    if (!mylib->ok()) {
        cerr << "Lib init failed: " <<
            mylib->errAsString("main", mylib->getInitError()) << endl;
        return 1;
    }
//    mylib->setLogFileName("/tmp/libupnp.log", LibUPnP::LogLevelDebug);

    if ((op_flags & OPT_l)) {
        while (true) {
            listDevices();
            sleep(5);
        }
    } else if ((op_flags & OPT_m)) {
        getMetadata(fname, arg);
    } else if ((op_flags & OPT_r)) {
        readdir(fname, arg);
    } else if ((op_flags & OPT_s)) {
        search(fname, arg);
    } else if ((op_flags & OPT_c)) {
        getSearchCaps(fname);
    } else if ((op_flags & OPT_V)) {
        int volume = atoi(arg.c_str());
        getsetVolume(fname, volume);
    } else if ((op_flags & OPT_v)) {
        getsetVolume(fname);
    } else if ((op_flags & OPT_M)) {
        tpMonitor(fname);
    } else if ((op_flags & OPT_p)) {
        int iarg = atoi(arg.c_str());
        tpPlayStop(fname, iarg);
    } else if ((op_flags & OPT_P)) {
        tpPause(fname);
    } else if ((op_flags & OPT_aa)) {
        return tpAlbumArt(fname);
    } else {
        Usage();
    }

    return 0;
}
