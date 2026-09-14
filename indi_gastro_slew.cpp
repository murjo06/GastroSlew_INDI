#include "indi_nakorak.h"

#include <indicom.h>
#include <connectionplugins/connectionserial.h>

#include <bitset>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>

#define GASTROSLEW_TIMEOUT          5
#define GASTROSLEW_BUFFER    		64

#define GASTROSLEW_CMD_START_CHAR	':'
#define GASTROSLEW_CMD_END_CHAR		'#'


static std::unique_ptr<GastroSlew> nakorak(new GastroSlew());

GastroSlew::GastroSlew(): GI(this)
{
    SetTelescopeCapability(TELESCOPE_CAN_PARK |
                           TELESCOPE_CAN_ABORT |
                           TELESCOPE_CAN_SYNC |
                           TELESCOPE_CAN_GOTO |
                           TELESCOPE_HAS_TIME |
                           TELESCOPE_HAS_LOCATION |
                           TELESCOPE_HAS_PIER_SIDE, NAKORAK_SLEW_RATES_NUMBER);

    SetParkDataType(PARK_RA_DEC);

    // Should be set to invalid first
    longitude = std::numeric_limits<double>::quiet_NaN();
    latitude  = std::numeric_limits<double>::quiet_NaN();

    setVersion(0, 1);
}

const char *GastroSlew::getDefaultName()
{
    return "Gastro Slew";
}

bool GastroSlew::initProperties()
{
    INDI::Telescope::initProperties();

    GI::initProperties(MOTION_TAB);

    serialConnection->setDefaultBaudRate(Connection::Serial::B_19200);
    serialConnection->setParity(1);

    addAuxControls();

    setDriverInterface(getDriverInterface() | GUIDER_INTERFACE);

    AddTrackMode("TRACK_SIDEREAL", "Sidereal", true);

    return true;
}

void TemmaMount::ISGetProperties(const char *dev)
{
    INDI::Telescope::ISGetProperties(dev);

    if(!isConnected()) {
        return;
    }

    defineProperty(&GuideRateNP);

    //todo: pid kalibracije
}

bool GastroSlew::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (GI::processNumber(dev, name, values, names, n))
        return true;

    return INDI::Telescope::ISNewNumber(dev, name, values, names, n);
}

bool GastroSlew::updateProperties()
{
    INDI::Telescope::updateProperties();

    if (isConnected())
    {
        TrackState = getMotorStatus() ? SCOPE_TRACKING : SCOPE_IDLE;

        double lst = get_local_sidereal_time(longitude);

        if (InitPark())
        {
            // If loading parking data is successful, we just set the default parking values.
            SetAxis1ParkDefault(range24(lst + 3 / 60.0));
            SetAxis2ParkDefault(latitude >= 0 ? 90 : -90);
        }
        else
        {
            // Otherwise, we set all parking data to default in case no parking data is found.
            SetAxis1Park(range24(lst + 3 / 60.0));
            SetAxis2Park(latitude >= 0 ? 90 : -90);
            SetAxis1ParkDefault(range24(lst + 3 / 60.0));
            SetAxis2ParkDefault(latitude >= 0 ? 90 : -90);
        }

        // Load location so that it could trigger mount initialization
        loadConfig(true, "GEOGRAPHIC_COORD");

    }

    GI::updateProperties();

    return true;
}

bool GastroSlew::getCoords()
{
    char response[GASTROSLEW_BUFFER];

    bool rc = sendCommand("e", response);
    if (rc == false)
        return false;

    if (response[0] != 'e' || response[9] != ',')
        return false;

    uint32_t coord_buffer = 0;
    sscanf(response + 1, "%08lx", &coord_buffer);

    float coord = 0.0f;
    memcpy(&coord, &coord_buffer, sizeof(float));
    currentRA = (double)coord;


    coord_buffer = 0;
    sscanf(response + 10, "%08lx", &coord_buffer);

    coord = 0.0f;
    memcpy(&coord, &coord_buffer, sizeof(float));
    currentDEC = (double)coord;

    char side = response[18];
    switch(side)
    {
        case 'E':
            setPierSide(PIER_EAST);
            break;

        case 'W':
            setPierSide(PIER_WEST);
            break;

        case 'F':       //? ????????????????????????
            //  lets see if our goto has finished
            if (strstr(response, "F") != nullptr)   //todo: ?????
            {
                if (TrackState == SCOPE_SLEWING)
                    TrackState = SCOPE_TRACKING;
                else if (TrackState == SCOPE_PARKING)
                {
                    SetParked(true);
                    //  turn off the motor
                    setMotorStatus(false);
                }
            }
            else
            {
                LOG_DEBUG("Goto in Progress...");
            }
            break;

    }

    return true;
}

bool GastroSlew::ReadScopeStatus()
{
    // JM: Do not read mount until it is initilaized.
    if (gastroSlewInitialized == false)
        return false;

    if (isSimulation())
    {
        return true;
    }

    bool rc = getCoords();

    if (rc == false)
        return false;

    alignedRA = currentRA;
    alignedDEC = currentDEC;

    NewRaDec(currentRA, currentDEC);

    return true;
}

bool GastroSlew::Sync(double ra, double dec)
{
    char cmd[GASTROSLEW_BUFFER] = {0}, res[GASTROSLEW_BUFFER] = {0};
    char sign;

    targetRA = ra;
    targetDEC = dec;

    setLST();

    uint32_t ra_buffer = 0;
    uint32_t dec_buffer = 0;

    float ra_f = (float)ra;
    float dec_f = (float)dec;

    memcpy(&ra_buffer, &ra_f, sizeof(float));
    memcpy(&dec_buffer, &dec_f, sizeof(float));

    snprintf(cmd, GASTROSLEW_BUFFER, "S%08lx,%08lx", ra_buffer, dec_buffer);

    if (sendCommand(cmd, res) == false)
        return false;

    //  if the first character is an R, it's a correct response
    if (res[0] != 'R')
        return false;

    if (res[1] != '0')
    {
        // 0 = success
        // 1 = RA error
        // 2 = Dec error
        // 3 = To many digits
        LOGF_DEBUG("Sync command failed, response: %s", res);
        return false;
    }

    return true;
}

bool GastroSlew::Goto(double ra, double dec)
{
    char cmd[GASTROSLEW_BUFFER] = {0}, res[GASTROSLEW_BUFFER] = {0};
    char sign;

    targetRA = ra;
    targetDEC = dec;

    /*  goto involves hoops, but, not as many as a sync
        first set sidereal time
        then issue the goto command
    */
    if (motorsEnabled == false)
    {
        LOG_DEBUG("Goto turns on motors");
        setMotorStatus(true);
    }

    setLST();

    uint32_t ra_buffer = 0;
    uint32_t dec_buffer = 0;

    float ra_f = (float)ra;
    float dec_f = (float)dec;

    memcpy(&ra_buffer, &ra_f, sizeof(float));
    memcpy(&dec_buffer, &dec_f, sizeof(float));

    snprintf(cmd, GASTROSLEW_BUFFER, "P%08lx,%08lx", ra_buffer, dec_buffer);

    if (sendCommand(cmd, res) == false)
        return false;

    //  if the first character is an R, it's a correct response
    if (res[0] != 'R')
        return false;
    if (res[1] != '0')
    {
        // 0 = success
        // 1 = RA error
        // 2 = Dec error
        // 3 = To many digits
        LOGF_DEBUG("Goto command failed, response: %s", res);
        return false;
    }

    TrackState = SCOPE_SLEWING;
    return true;
}

bool GastroSlew::Park()
{
    double lst = get_local_sidereal_time(longitude);

    // Set Axis 1 parking to LST + 3 minutes as a safe offset
    // so that a GOTO to it would work
    // this would end up with mount looking at pole with counter-weight down
    SetAxis1Park(range24(lst + 3 / 60.0));
    LOGF_DEBUG("heading to Park position %4.2f %4.2f", GetAxis1Park(), GetAxis2Park());

    Goto(GetAxis1Park(), GetAxis2Park());

    TrackState = SCOPE_PARKING;
    LOG_INFO("Parking is in progress...");

    return true;
}

bool GastroSlew::UnPark()
{
    // Get LST and set it as Axis1 Park position
    double lst = get_local_sidereal_time(longitude);
    SetAxis1Park(lst);

    LOGF_INFO("Syncing to Park position %4.2f %4.2f", GetAxis1Park(), GetAxis2Park());
    Sync(GetAxis1Park(), GetAxis2Park());

    SetParked(false);

    setMotorStatus(true);
    if (motorsEnabled)
        TrackState = SCOPE_TRACKING;
    else
        TrackState = SCOPE_IDLE;

    return true;
}

bool GastroSlew::SetCurrentPark()
{
    SetAxis1Park(currentRA);
    SetAxis2Park(currentDEC);
    return true;
}

bool GastroSlew::SetDefaultPark()
{
    double lst = get_local_sidereal_time(longitude);
    SetAxis1Park(range24(lst + 3.0 / 60.0));
    SetAxis2Park(latitude >= 0.0 ? 90.0 : -90.0);

    return true;
}

bool GastroSlew::Abort()
{
    char res[GASTROSLEW_BUFFER] = {0};

    if (sendCommand("A") == false)
        return false;

    return true;
}

bool GastroSlew::sendGuideCommand(char direction, uint32_t ms)
{
	char cmd[GASTROSLEW_BUFFER] = {0};
	snprintf(cmd, GASTROSLEW_BUFFER, "G%c%04d", direction, ms);

	return sendCommand(cmd);
}

IPState GastroSlew::GuideNorth(uint32_t ms)
{
    sendGuideCommand('N', ms);
}

IPState GastroSlew::GuideSouth(uint32_t ms)
{
    sendGuideCommand('S', ms);
}

IPState GastroSlew::GuideEast(uint32_t ms)
{
	sendGuideCommand('E', ms);
}

IPState GastroSlew::GuideWest(uint32_t ms)
{
    sendGuideCommand('W', ms);
}

bool GastroSlew::updateLocation(double latitude, double longitude, double elevation)
{
    INDI_UNUSED(elevation);

    longitude = longitude;
    latitude = latitude;

    //  A temma mount must have the LST and latitude set
    //  Prior to these being set, reads will return garbage
    if (!gastroSlewInitialized)
    {
		double lst = get_local_sidereal_time(longitude);

        setLatitude(latitude);
        setLST();

        gastroSlewInitialized = true;

        //  We were NOT initialized, so, in case there is not park position set
        //  Sync to the position of bar vertical, telescope pointed at pole
        LOGF_INFO("GastroSlew is initialized. latitude: %.2f LST: %.2f", latitude, lst);
        SetAxis1Park(lst);

        LOGF_INFO("Syncing to default home position %4.2f %4.2f", GetAxis1Park(), GetAxis2Park());
        Sync(GetAxis1Park(), GetAxis2Park());
    }

    return true;
}

bool GastroSlew::getVersion()
{
    char res[GASTROSLEW_BUFFER] = {0};
    for (int i = 0; i < 3; i++)
    {
        if (sendCommand("v", res) && res[0] == 'v')
            break;
		LOG_INFO("Read version failed, retrying in 100 ms.");
        usleep(100000);
    }

    if (res[0] != 'v')
    {
        LOG_ERROR("Read version failed.");
        return false;
    }

    LOGF_INFO("Detected version: %s", res + 1);

    return true;
}

bool GastroSlew::getMotorStatus()
{
    char res[GASTROSLEW_BUFFER] = {0};

    if (sendCommand("m", res) == false)
        return false;

    if (strstr(res, "+") != nullptr)
        motorsEnabled = true;
    else
        motorsEnabled = false;

    LOGF_DEBUG("Motor is %s", res);

    return motorsEnabled;
}

bool GastroSlew::setMotorStatus(bool enable)
{
    char res[GASTROSLEW_BUFFER] = {0};
    bool rc = false;

    // M+ --> Motor ON
    // M- --> Motor OFF
    if (enable)
        rc = sendCommand("M+", res);
    else
        rc = sendCommand("M-", res);

    if (rc == false)
        return false;

    getMotorStatus();

    return true;
}

/*  bit of a hack, returns true if lst is a sane value, false if it is not sane */
bool GastroSlew::getLST(double &lst)
{
    char res[GASTROSLEW_BUFFER] = {0};

    if (sendCommand("l", res) == false)
        return false;
    
    if(res[0] != 'l')
        return false;

    uint32_t lst_buffer = 0;

    sscanf(res + 1, "%08lx", lst_buffer);

    float lst_f = 0.0f;
    memcpy(&lst_f, &lst_buffer, sizeof(float));
    lst = (double)lst_f;

    return (lst >= 0.0 && lst <= 24.0);
}

bool GastroSlew::setLST()
{
    char cmd[GASTROSLEW_BUFFER] = {0};
    float lst = (float)get_local_sidereal_time(longitude);

    uint32_t lst_buffer = 0;
    memcpy(&lst_buffer, &lst, sizeof(float));

    snprintf(cmd, GASTROSLEW_BUFFER, "L%08lx", lst_buffer);

    return sendCommand(cmd);
}

bool GastroSlew::getLatitude(double &lat)
{
    char res[GASTROSLEW_BUFFER] = {0};

    if (sendCommand("f", res) == false)
        return false;

    if(res[0] != 'f') {
        return false;
    }

    uint32_t lat_buffer = 0;
    sscanf(res + 1, "%08lx", &lat_buffer);

    float lat_f = 0.0f;
    memcpy(&lat_f, &lat_buffer, sizeof(float));
    lat = (double)lat_f;

    return (lat >= -90.0 && lat <= 90.0);
}

bool GastroSlew::setLatitude(double lat)
{
    char cmd[GASTROSLEW_BUFFER];

    uint32_t lat_buffer = 0;
    float lat_f = (float)lat;

    memcpy(&lat_buffer, &lat_f, sizeof(float));

    snprintf(cmd, GASTROSLEW_BUFFER, "F%08lx", lat_buffer);

    return sendCommand(cmd);
}

bool GastroSlew::Handshake()
{
    //LOG_DEBUG("Calling get version");
    /*
        This is an ugly hack, but, it works
        On first open we often dont get an immediate read from the temma
        but it seems to read much more reliably if we enforce a short wait
        between opening the port and doing the first query for version

    */
    usleep(100);
    if (getVersion() == false)
        return false;

    double lst = 0;
    if (getLST(lst))
    {
        LOG_DEBUG("GastroSlew is initialized.");
        gastroSlewInitialized = true;
    }
    else
    {
        LOG_DEBUG("GastroSlew is not initialized.");
        gastroSlewInitialized = false;
    }

    getMotorStatus();

    return true;
}

bool GastroSlew::SetTrackMode(uint8_t mode)
{
    if (mode == TRACK_SIDEREAL)
        return sendCommand("T");

    return false;
}

bool GastroSlew::SetTrackEnabled(bool enabled)
{
    if (enabled)
    {
        setMotorStatus(true);
        return SetTrackMode(TrackModeSP.findOnSwitchIndex());
    }
    else
    {
        setMotorStatus(false);
        return sendCommand("C");
    }
}

bool GastroSlew::sendCommand(const char *command, char *response)
{
    if (isSimulation())
        return true;
    if (command == nullptr)
        return false;
    
    int nbytes_written = 0, nbytes_read = 0, rc = -1;
    tcflush(PortFD, TCIFLUSH);
    size_t len = strlen(command);
    if(len > GASTROSLEW_BUFFER) {    // command too long
        return false;
    }
    char cmd[len + 3];
    memcpy(cmd + 1, command, sizeof(char) * len);
    cmd[0] = GASTROSLEW_CMD_START_CHAR;
    cmd[len + 1] = GASTROSLEW_CMD_END_CHAR;
    cmd[len + 2] = '\0';
    LOGF_DEBUG("CMD %s", cmd);

    if ((rc = tty_write_string(PortFD, cmd, &nbytes_written)) != TTY_OK)
    {
        char errstr[MAXRBUF] = {0};
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Serial write error: %s.", errstr);
        return false;
    }

    if (response == nullptr)
    {
        tcdrain(PortFD);
        return true;
    }

    char res[GASTROSLEW_BUFFER] = {0};

    if ((rc = tty_nread_section(PortFD, res, GASTROSLEW_BUFFER - 1, GASTROSLEW_CMD_END_CHAR, GASTROSLEW_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        char errstr[MAXRBUF] = {0};
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("Serial read error: %s.", errstr);
        return false;
    }

    if(res[0] != GASTROSLEW_CMD_START_CHAR) {
        return false;
    }

    res[nbytes_read - 1] = '\0';

    memcpy(response, res + 1, nbytes_read - 2);

    response[nbytes_read - 3] = '\0';

    LOGF_DEBUG("RES %s", response);
    tcflush(PortFD, TCIOFLUSH);

    return true;
}