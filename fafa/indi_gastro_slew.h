#pragma once

#include "indiguiderinterface.h"
#include "inditelescope.h"

class GastroSlew : public INDI::Telescope, public INDI::GuiderInterface
{
    public:
        GastroSlew();
        virtual ~GastroSlew() override = default;

        virtual bool initProperties() override;
        virtual bool updateProperties() override;
        virtual const char *getDefaultName() override;

    	virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;


    protected:
        virtual bool Handshake() override;
        virtual bool ReadScopeStatus() override;

        virtual bool Goto(double ra, double dec) override;
        virtual bool Sync(double ra, double dec) override;
        virtual bool Park() override;
        virtual bool UnPark() override;
        virtual bool Abort() override;
        virtual bool SetTrackEnabled(bool enabled) override;

        virtual bool updateLocation(double latitude, double longitude, double elevation) override;

        virtual bool SetCurrentPark() override;
        virtual bool SetDefaultPark() override;

        virtual IPState GuideNorth(uint32_t ms) override;
        virtual IPState GuideSouth(uint32_t ms) override;
        virtual IPState GuideEast(uint32_t ms) override;
        virtual IPState GuideWest(uint32_t ms) override;


    private:
        bool getVersion();
        bool getCoords();

        bool sendCommand(const char *cmd, char *response = nullptr);

        bool getMotorStatus();
        bool setMotorStatus(bool enable);

        bool setLST();
        bool getLST(double &lst);
        bool setLatitude(double lat);
        bool getLatitude(double &lat);

        bool sendGuideCommand(char direction, uint32_t ms);

        double currentRA = 0, currentDEC = 0, targetRA = 0, targetDEC = 0, alignedRA = 0, alignedDEC = 0;

        bool motorsEnabled { false };
        bool gastroSlewInitialized { false };
        double longitude { 0 };
        double latitude { 0 };
};