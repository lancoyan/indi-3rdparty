/*
    indi_interferometer - a telescope array driver for INDI
    Support for AHP cross-correlators
    Copyright (C) 2020  Ilia Platone

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/file.h>
#include <memory>
#include <indicom.h>
#include <connectionplugins/connectionserial.h>
#include "indi_ahp_correlator.h"

static std::unique_ptr<AHP_XC> array(new AHP_XC());

void ISGetProperties(const char *dev)
{
    array->ISGetProperties(dev);
}

void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int num)
{
    array->ISNewSwitch(dev, name, states, names, num);
}

void ISNewText(    const char *dev, const char *name, char *texts[], char *names[], int num)
{
    array->ISNewText(dev, name, texts, names, num);
}

void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int num)
{
    array->ISNewNumber(dev, name, values, names, num);
}

void ISNewBLOB (const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
    array->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);
}

void ISSnoopDevice (XMLEle *root)
{
    array->ISSnoopDevice(root);
}

void AHP_XC::Callback()
{
    unsigned long* counts = static_cast<unsigned long*>(malloc(static_cast<unsigned int>(ahp_xc_get_nlines())));
    correlation* crosscorrelations = static_cast<correlation*>(malloc(sizeof(correlation)*static_cast<unsigned int>(ahp_xc_get_nbaselines()*(ahp_xc_get_crosscorrelator_jittersize()*2-1))));
    int w = PrimaryCCD.getXRes();
    int h = PrimaryCCD.getYRes();
    double *framebuffer = static_cast<double*>(malloc(static_cast<unsigned int>(w*h)*sizeof(double)));
    char str[MAXINDINAME];
    str[ahp_xc_get_bps()] = 0;

    EnableCapture(true);
    threadsRunning = true;
    while (threadsRunning)
    {
        ahp_xc_get_packet(counts, nullptr, crosscorrelations);
        timeleft = ExposureRequest-(getCurrentTime()-ExposureStart);

        if(InExposure) {
            if(timeleft <= 0.0f) {
                // We're no longer exposing...
                AbortExposure();
                /* We're done exposing */
                LOG_INFO("Exposure done, downloading image...");
                dsp_buffer_stretch(framebuffer, w*h, 0.0, 65535.0);
                dsp_buffer_copy(framebuffer, static_cast<unsigned short*>(static_cast<void*>(PrimaryCCD.getFrameBuffer())), w*h);
                // Let INDI::CCD know we're done filling the image buffer
                LOG_INFO("Download complete.");
                ExposureComplete(&PrimaryCCD);
            }
        } else {
            memset(framebuffer, 0, static_cast<unsigned int>(w*h)*sizeof(double));
        }

        int idx = 0;
        double minalt = 0;
        int farest = 0;
        for(int x = 0; x < ahp_xc_get_nlines(); x++) {
            totalcounts[x] += counts[x];
            for(int y = x+1; y < ahp_xc_get_nlines(); y++) {
                totalcorrelations[idx*ahp_xc_get_crosscorrelator_jittersize()+ahp_xc_get_crosscorrelator_jittersize()-1] += crosscorrelations[idx].correlations;
                if(InExposure) {
                    if(lineEnableSP[x].sp[0].s == ISS_ON&&lineEnableSP[y].sp[0].s == ISS_ON) {
                        INDI::Correlator::UVCoordinate uv = baselines[idx]->getUVCoordinates();
                        int xx = static_cast<int>(w*uv.u/2.0);
                        int yy = static_cast<int>(h*uv.v/2.0);
                        int z = w*h/2+w/2+xx+yy*w;
                        if(xx >= -w/2 && xx < w/2 && yy >= -w/2 && yy < h/2) {
                            framebuffer[z] += (double)crosscorrelations[idx].coherence;
                            framebuffer[w*h-1-z] += (double)crosscorrelations[idx].coherence;
                        }
                    }
                }
                double lst = get_local_sidereal_time(lineGPSNP[x].np[1].value);
                double ha = get_local_hour_angle(lst, lineTelescopeNP[x].np[0].value);
                get_alt_az_coordinates(ha, lineTelescopeNP[x].np[1].value, lineGPSNP[x].np[0].value, &alt[x], &az[x]);
                farest = (minalt < alt[x] ? farest : x);
                minalt = (minalt < alt[x] ? minalt : alt[x]);
                idx++;
            }
        }
        delay[farest] = 0;
        idx = 0;
        for(int x = 0; x < ahp_xc_get_nlines(); x++) {
            for(int y = x+1; y < ahp_xc_get_nlines(); y++) {
                INDI::Correlator::Baseline b = baselines[idx]->getBaseline();
                double d = sqrt(pow(b.x, 2) + pow(b.y, 2) + pow(b.z, 2));
                idx++;
                double t = minalt*M_PI/180.0;
                if(x == farest) {
                    t -=  alt[y]*M_PI/180.0;
                    delay[y] = d * cos(t);
                }
                if(y == farest) {
                    t -=  alt[x]*M_PI/180.0;
                    delay[x] = d * cos(t);
                }
            }
        }
        for(int x = 0; x < ahp_xc_get_nlines(); x++) {
            int delay_clocks = delay[x] * ahp_xc_get_frequency() / LIGHTSPEED;
            delay_clocks = (delay_clocks > 0 ? (delay_clocks < ahp_xc_get_delaysize() ? delay_clocks : ahp_xc_get_delaysize()-1) : 0);
            ahp_xc_set_delay(x, delay_clocks);
        }
    }
    EnableCapture(false);
    free (framebuffer);
}

AHP_XC::AHP_XC()
{
    clock_divider = 0;

    ExposureRequest = 0.0;
    InExposure = false;

    lineStatsN = static_cast<INumber*>(malloc(1));
    lineStatsNP = static_cast<INumberVectorProperty*>(malloc(1));

    lineEnableS = static_cast<ISwitch*>(malloc(1));
    lineEnableSP = static_cast<ISwitchVectorProperty*>(malloc(1));

    linePowerS = static_cast<ISwitch*>(malloc(1));
    linePowerSP = static_cast<ISwitchVectorProperty*>(malloc(1));

    lineDevicesT = static_cast<IText*>(malloc(1));
    lineDevicesTP = static_cast<ITextVectorProperty*>(malloc(1));

    snoopGPSN = static_cast<INumber*>(malloc(1));
    snoopGPSNP = static_cast<INumberVectorProperty*>(malloc(1));

    snoopTelescopeN = static_cast<INumber*>(malloc(1));
    snoopTelescopeNP = static_cast<INumberVectorProperty*>(malloc(1));

    snoopTelescopeInfoN = static_cast<INumber*>(malloc(1));
    snoopTelescopeInfoNP = static_cast<INumberVectorProperty*>(malloc(1));

    snoopDomeN = static_cast<INumber*>(malloc(1));
    snoopDomeNP = static_cast<INumberVectorProperty*>(malloc(1));

    lineDelayN = static_cast<INumber*>(malloc(1));
    lineDelayNP = static_cast<INumberVectorProperty*>(malloc(1));

    lineGPSN = static_cast<INumber*>(malloc(1));
    lineGPSNP = static_cast<INumberVectorProperty*>(malloc(1));

    lineTelescopeN = static_cast<INumber*>(malloc(1));
    lineTelescopeNP = static_cast<INumberVectorProperty*>(malloc(1));

    lineDomeN = static_cast<INumber*>(malloc(1));
    lineDomeNP = static_cast<INumberVectorProperty*>(malloc(1));

    correlationsN = static_cast<INumber*>(malloc(1));

    totalcounts = static_cast<double*>(malloc(1));
    totalcorrelations = static_cast<double*>(malloc(1));
    alt = static_cast<double*>(malloc(1));
    az = static_cast<double*>(malloc(1));
    delay = static_cast<double*>(malloc(1));
    baselines = static_cast<baseline**>(malloc(1));
}

bool AHP_XC::Disconnect()
{
    for(int x = 0; x < ahp_xc_get_nlines(); x++)
        ActiveLine(x, false, false);

    threadsRunning = false;
    usleep(1000000);
    readThread->join();
    readThread->~thread();

    ahp_xc_disconnect();


    return true;
}

const char * AHP_XC::getDefaultName()
{
    return "AHP XC Correlator";
}

const char * AHP_XC::getDeviceName()
{
    return getDefaultName();
}

bool AHP_XC::saveConfigItems(FILE *fp)
{
    for(int x = 0; x < ahp_xc_get_nlines(); x++) {
        IUSaveConfigSwitch(fp, &lineEnableSP[x]);
        if(lineEnableSP[x].sp[0].s == ISS_ON) {
            IUSaveConfigText(fp, &lineDevicesTP[x]);
            IUSaveConfigSwitch(fp, &linePowerSP[x]);
        }
    }
    IUSaveConfigNumber(fp, &settingsNP);

    INDI::CCD::saveConfigItems(fp);
    return true;
}

/**************************************************************************************
** INDI is asking us to init our properties.
***************************************************************************************/
bool AHP_XC::initProperties()
{

    // Must init parent properties first!
    INDI::CCD::initProperties();

    SetCCDCapability(CCD_CAN_ABORT|CCD_CAN_SUBFRAME|CCD_HAS_DSP);

    IUFillNumber(&settingsN[0], "INTERFEROMETER_WAVELENGTH_VALUE", "Filter wavelength (m)", "%g", 3.0E-12, 3.0E+3, 1.0E-9, 0.211121449);
    IUFillNumber(&settingsN[1], "INTERFEROMETER_BANDWIDTH_VALUE", "Filter bandwidth (m)", "%g", 3.0E-12, 3.0E+3, 1.0E-9, 1199.169832);
    IUFillNumberVector(&settingsNP, settingsN, 2, getDeviceName(), "INTERFEROMETER_SETTINGS", "AHP_XC Settings", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    // Set minimum exposure speed to 0.001 seconds
    PrimaryCCD.setMinMaxStep("CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", 1.0, STELLAR_DAY, 1, false);
    setDefaultPollingPeriod(500);

    serialConnection = new Connection::Serial(this);
    serialConnection->setStopBits(2);
    serialConnection->setDefaultBaudRate(Connection::Serial::B_57600);
    registerConnection(serialConnection);

    return true;
}

/**************************************************************************************
** INDI is asking us to submit list of properties for the device
***************************************************************************************/
void AHP_XC::ISGetProperties(const char *dev)
{
    INDI::CCD::ISGetProperties(dev);

    if (isConnected())
    {
        for (int x=0; x<ahp_xc_get_nlines(); x++) {
            defineSwitch(&lineEnableSP[x]);
        }
        defineNumber(&correlationsNP);
        defineNumber(&settingsNP);

        // Define our properties
    }
}

/********************************************************************************************
** INDI is asking us to update the properties because there is a change in CONNECTION status
** This fucntion is called whenever the device is connected or disconnected.
*********************************************************************************************/
bool AHP_XC::updateProperties()
{
    // Call parent update properties
    INDI::CCD::updateProperties();

    if (isConnected())
    {

        // Let's get parameters now from CCD
        setupParams();

        for (int x=0; x<ahp_xc_get_nlines(); x++) {
            defineSwitch(&lineEnableSP[x]);
        }
        defineNumber(&correlationsNP);
        defineNumber(&settingsNP);
    }
    else
        // We're disconnected
    {
        deleteProperty(correlationsNP.name);
        deleteProperty(settingsNP.name);
        for (int x=0; x<ahp_xc_get_nlines(); x++) {
            deleteProperty(lineEnableSP[x].name);
            deleteProperty(linePowerSP[x].name);
            deleteProperty(lineGPSNP[x].name);
            deleteProperty(lineTelescopeNP[x].name);
            deleteProperty(lineStatsNP[x].name);
            deleteProperty(lineDevicesTP[x].name);
            deleteProperty(lineDelayNP[x].name);
        }
    }

    for(int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->updateProperties();

    return true;
}

/**************************************************************************************
** Setting up CCD parameters
***************************************************************************************/
void AHP_XC::setupParams()
{
    SetCCDParams(MAX_RESOLUTION, MAX_RESOLUTION, 16,  PIXEL_SIZE, PIXEL_SIZE);

    // Let's calculate how much memory we need for the primary CCD buffer
    int nbuf;
    nbuf = PrimaryCCD.getXRes() * PrimaryCCD.getYRes() * PrimaryCCD.getBPP() / 8;
    nbuf += 512;  //  leave a little extra at the end
    PrimaryCCD.setFrameBufferSize(static_cast<unsigned int>(nbuf));
    memset(PrimaryCCD.getFrameBuffer(), 0, static_cast<unsigned int>(PrimaryCCD.getFrameBufferSize()));
}

/**************************************************************************************
** Client is asking us to start an exposure
***************************************************************************************/
bool AHP_XC::StartExposure(float duration)
{
    if(InExposure)
        return false;

    ExposureStart = getCurrentTime();
    ExposureRequest = duration;
    timeleft = ExposureRequest;
    PrimaryCCD.setExposureDuration(static_cast<double>(ExposureRequest));
    InExposure = true;
    // We're done
    return true;
}

/**************************************************************************************
** Client is asking us to abort an exposure
***************************************************************************************/
bool AHP_XC::AbortExposure()
{
    InExposure = false;
    return true;
}

/**************************************************************************************
** Client is asking us to set a new number
***************************************************************************************/
bool AHP_XC::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (strcmp (dev, getDeviceName()))
        return false;

    for(int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewNumber(dev, name, values, names, n);

    if(!strcmp(settingsNP.name, name)) {
        IUUpdateNumber(&settingsNP, values, names, n);
        for(int x = 0; x < ahp_xc_get_nbaselines(); x++) {
            baselines[x]->setWavelength(settingsN[0].value);
        }
        IDSetNumber(&settingsNP, nullptr);
        return true;
    }
    return INDI::CCD::ISNewNumber(dev, name, values, names, n);
}

/**************************************************************************************
** Client is asking us to set a new switch
***************************************************************************************/
bool AHP_XC::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (strcmp (dev, getDeviceName()))
        return false;

    if(!strcmp(name, "DEVICE_BAUD_RATE")) {
        if(isConnected()) {
            if(states[0] == ISS_ON || states[1] == ISS_ON || states[2] == ISS_ON) {
                states[0] = states[1] = states[2] = ISS_OFF;
                states[3] = ISS_ON;
            }
            IUUpdateSwitch(getSwitch("DEVICE_BAUD_RATE"), states, names, n);
            if (states[3] == ISS_ON) {
                ahp_xc_set_baudrate(R_57600);
            }
            if (states[4] == ISS_ON) {
                ahp_xc_set_baudrate(R_115200);
            }
            if (states[5] == ISS_ON) {
                ahp_xc_set_baudrate(R_230400);
            }
            IDSetSwitch(getSwitch("DEVICE_BAUD_RATE"), nullptr);
        }
    }

    for(int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewSwitch(dev, name, states, names, n);

    for(int x = 0; x < ahp_xc_get_nlines(); x++) {
        if(!strcmp(name, lineEnableSP[x].name)){
            IUUpdateSwitch(&lineEnableSP[x], states, names, n);
            if(lineEnableSP[x].sp[0].s == ISS_ON) {
                ActiveLine(x, true, linePowerSP[x].sp[0].s == ISS_ON);
                defineSwitch(&linePowerSP[x]);
                defineNumber(&lineGPSNP[x]);
                defineNumber(&lineTelescopeNP[x]);
                defineNumber(&lineDelayNP[x]);
                defineNumber(&lineStatsNP[x]);
                defineText(&lineDevicesTP[x]);
            } else {
                ActiveLine(x, false, false);
                deleteProperty(linePowerSP[x].name);
                deleteProperty(lineGPSNP[x].name);
                deleteProperty(lineTelescopeNP[x].name);
                deleteProperty(lineStatsNP[x].name);
                deleteProperty(lineDevicesTP[x].name);
                deleteProperty(lineDelayNP[x].name);
            }
            IDSetSwitch(&lineEnableSP[x], nullptr);
        }
        if(!strcmp(name, linePowerSP[x].name)){
            IUUpdateSwitch(&linePowerSP[x], states, names, n);
            ActiveLine(x, true, linePowerSP[x].sp[0].s == ISS_ON);
            IDSetSwitch(&linePowerSP[x], nullptr);
        }
    }
    return INDI::CCD::ISNewSwitch(dev, name, states, names, n);
}

/**************************************************************************************
** Client is asking us to set a new BLOB
***************************************************************************************/
bool AHP_XC::ISNewBLOB(const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
    if (strcmp (dev, getDeviceName()))
        return false;

    for(int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);

    return INDI::CCD::ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);
}

/**************************************************************************************
** Client is asking us to set a new text
***************************************************************************************/
bool AHP_XC::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (strcmp (dev, getDeviceName()))
        return false;

    //  This is for our device
    //  Now lets see if it's something we process here
    for(int x = 0; x < ahp_xc_get_nlines(); x++) {
        if (!strcmp(name, lineDevicesTP[x].name))
        {
            lineDevicesTP[x].s = IPS_OK;
            IUUpdateText(&lineDevicesTP[x], texts, names, n);
            IDSetText(&lineDevicesTP[x], nullptr);

            // Update the property name!
            strncpy(snoopTelescopeNP[x].device, lineDevicesTP[x].tp[0].text, MAXINDIDEVICE);
            strncpy(snoopTelescopeInfoNP[x].device, lineDevicesTP[x].tp[0].text, MAXINDIDEVICE);
            strncpy(snoopGPSNP[x].device, lineDevicesTP[x].tp[1].text, MAXINDIDEVICE);
            strncpy(snoopDomeNP[x].device, lineDevicesTP[x].tp[2].text, MAXINDIDEVICE);

            IDSnoopDevice(lineDevicesTP[x].tp[0].text, "EQUATORIAL_EOD_COORD");
            IDSnoopDevice(lineDevicesTP[x].tp[0].text, "TELESCOPE_INFO");
            IDSnoopDevice(snoopGPSNP[x].device, "GEOGRAPHIC_COORD");
            IDSnoopDevice(snoopDomeNP[x].device, "GEOGRAPHIC_COORD");

            //  We processed this one, so, tell the world we did it
            return true;
        }
    }

    for(int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISNewText(dev, name, texts, names, n);

    return INDI::CCD::ISNewText(dev, name, texts, names, n);
}

/**************************************************************************************
** Client is asking us to set a new snoop device
***************************************************************************************/
bool AHP_XC::ISSnoopDevice(XMLEle *root)
{
    for(int i = 0; i < ahp_xc_get_nlines(); i++) {
        if(!IUSnoopNumber(root, &snoopTelescopeNP[i])) {
            lineTelescopeNP[i].s = IPS_BUSY;
            lineTelescopeNP[i].np[0].value = snoopTelescopeNP[i].np[0].value;
            lineTelescopeNP[i].np[1].value = snoopTelescopeNP[i].np[1].value;
            IDSetNumber(&lineTelescopeNP[i], nullptr);
        }
        if(!IUSnoopNumber(root, &snoopTelescopeInfoNP[i])) {
            lineTelescopeNP[i].s = IPS_BUSY;
            lineTelescopeNP[i].np[2].value = snoopTelescopeInfoNP[i].np[0].value;
            lineTelescopeNP[i].np[3].value = snoopTelescopeInfoNP[i].np[1].value;
            IDSetNumber(&lineTelescopeNP[i], nullptr);
        }
        if(!IUSnoopNumber(root, &snoopGPSNP[i])) {
            lineGPSNP[i].s = IPS_BUSY;
            double Lat0, Lon0;
            lineGPSNP[i].np[0].value = snoopGPSNP[i].np[0].value;
            lineGPSNP[i].np[1].value = snoopGPSNP[i].np[1].value;
            lineGPSNP[i].np[2].value = snoopGPSNP[i].np[2].value;
            int idx = 0;
            for(int x = 0; x < ahp_xc_get_nlines(); x++) {
                for(int y = x+1; y < ahp_xc_get_nlines(); y++) {
                    if(x==i||y==i) {
                        double Lat1, Lon1;
                        Lat0 = snoopGPSNP[x].np[0].value*M_PI/180.0;
                        Lon0 = snoopGPSNP[x].np[1].value*M_PI/180.0;
                        Lat1 = snoopGPSNP[y].np[0].value*M_PI/180.0;
                        Lon1 = snoopGPSNP[y].np[1].value*M_PI/180.0;
                        double radius = (EARTHRADIUSPOLAR+snoopGPSNP[y].np[2].value)+(EARTHRADIUSEQUATORIAL-EARTHRADIUSPOLAR)*cos(Lat0);
                        double x0 = cos(Lat0)*cos(Lon0)*radius;
                        double y0 = cos(Lat0)*sin(Lon0)*radius;
                        double z0 = sin(Lat0)*radius;
                        radius = (EARTHRADIUSPOLAR+snoopGPSNP[y].np[2].value)+(EARTHRADIUSEQUATORIAL-EARTHRADIUSPOLAR)*cos(Lat1);
                        double x1 = cos(Lat1)*cos(Lon1)*radius;
                        double y1 = cos(Lat1)*sin(Lon1)*radius;
                        double z1 = sin(Lat1)*radius;
                        INDI::Correlator::Baseline b;
                        b.x = (x0-x1);
                        b.y = (y0-y1);
                        b.z = (z0-z1);
                        baselines[idx]->setBaseline(b);
                    }
                    idx++;
                }
            }
            IDSetNumber(&lineGPSNP[i], nullptr);
        }
    }

    for(int x = 0; x < ahp_xc_get_nbaselines(); x++)
        baselines[x]->ISSnoopDevice(root);

    return INDI::CCD::ISSnoopDevice(root);
}

/**************************************************************************************
** INDI is asking us to add any FITS keywords to the FITS header
***************************************************************************************/
void AHP_XC::addFITSKeywords(fitsfile *fptr, INDI::CCDChip *targetChip)
{
    // Let's first add parent keywords
    INDI::CCD::addFITSKeywords(fptr, targetChip);

    // Add temperature to FITS header
    int status = 0;
    fits_write_date(fptr, &status);

}

/**************************************************************************************
** Main device loop. We check for exposure and temperature progress here
***************************************************************************************/
void AHP_XC::TimerHit()
{
    if(!isConnected())
        return;  //  No need to reset timer if we are not connected anymore

    if(InExposure) {
        // Just update time left in client
        PrimaryCCD.setExposureLeft(static_cast<double>(timeleft));
    }

    int idx = 0;
    IDSetNumber(&correlationsNP, nullptr);
    for (int x = 0; x < ahp_xc_get_nlines(); x++) {
        double line_delay = delay[x];
        double steradian = pow(asin(lineTelescopeNP[x].np[2].value*0.5/lineTelescopeNP[x].np[3].value), 2);
        double photon_flux = ((double)totalcounts[x])*1000.0/POLLMS;
        double photon_flux0 = calc_photon_flux(0, settingsNP.np[1].value, settingsNP.np[0].value, steradian);
        IDSetNumber(&lineDelayNP[x], nullptr);
        lineDelayNP[x].s = IPS_BUSY;
        lineDelayNP[x].np[0].value = line_delay;
        IDSetNumber(&lineStatsNP[x], nullptr);
        lineStatsNP[x].s = IPS_BUSY;
        lineStatsNP[x].np[0].value = ((double)totalcounts[x])*1000.0/(double)POLLMS;
        lineStatsNP[x].np[1].value = photon_flux/LUMEN(settingsNP.np[0].value);
        lineStatsNP[x].np[2].value = photon_flux0/LUMEN(settingsNP.np[0].value);
        lineStatsNP[x].np[3].value = calc_rel_magnitude(photon_flux, settingsNP.np[1].value, settingsNP.np[0].value, steradian);
        for(int y = x+1; y < ahp_xc_get_nlines(); y++) {
            correlationsNP.np[idx*2+0].value = (double)totalcorrelations[idx]*1000.0/(double)POLLMS;
            correlationsNP.np[idx*2+1].value = (double)totalcorrelations[idx]*2.0/(double)(totalcounts[x]+totalcounts[y]);
            totalcorrelations[idx] = 0;
            idx++;
        }
        totalcounts[x] = 0;
    }

    SetTimer(POLLMS);

    return;
}

bool AHP_XC::Connect()
{
    ahp_xc_connect(serialConnection->port());

    if(0 != ahp_xc_get_properties())
        return false;

    lineStatsN = static_cast<INumber*>(realloc(lineStatsN, static_cast<unsigned long>(4*ahp_xc_get_nlines())*sizeof(INumber)+1));
    lineStatsNP = static_cast<INumberVectorProperty*>(realloc(lineStatsNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    lineEnableS = static_cast<ISwitch*>(realloc(lineEnableS, static_cast<unsigned long>(ahp_xc_get_nlines())*2*sizeof(ISwitch)));
    lineEnableSP = static_cast<ISwitchVectorProperty*>(realloc(lineEnableSP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(ISwitchVectorProperty)+1));

    linePowerS = static_cast<ISwitch*>(realloc(linePowerS, static_cast<unsigned long>(ahp_xc_get_nlines())*2*sizeof(ISwitch)+1));
    linePowerSP = static_cast<ISwitchVectorProperty*>(realloc(linePowerSP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(ISwitchVectorProperty)+1));

    lineDevicesT = static_cast<IText*>(realloc(lineDevicesT, static_cast<unsigned long>(3*ahp_xc_get_nlines())*sizeof(IText)+1));
    lineDevicesTP = static_cast<ITextVectorProperty*>(realloc(lineDevicesTP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(ITextVectorProperty)+1));

    lineGPSN = static_cast<INumber*>(realloc(lineGPSN, static_cast<unsigned long>(3*ahp_xc_get_nlines())*sizeof(INumber)+1));
    lineGPSNP = static_cast<INumberVectorProperty*>(realloc(lineGPSNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    lineTelescopeN = static_cast<INumber*>(realloc(lineTelescopeN, static_cast<unsigned long>(4*ahp_xc_get_nlines())*sizeof(INumber)+1));
    lineTelescopeNP = static_cast<INumberVectorProperty*>(realloc(lineTelescopeNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    lineDomeN = static_cast<INumber*>(realloc(lineDomeN, static_cast<unsigned long>(2*ahp_xc_get_nlines())*sizeof(INumber)+1));
    lineDomeNP = static_cast<INumberVectorProperty*>(realloc(lineDomeNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    snoopGPSN = static_cast<INumber*>(realloc(snoopGPSN, static_cast<unsigned long>(3*ahp_xc_get_nlines())*sizeof(INumber)+1));
    snoopGPSNP = static_cast<INumberVectorProperty*>(realloc(snoopGPSNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    snoopTelescopeN = static_cast<INumber*>(realloc(snoopTelescopeN, static_cast<unsigned long>(2*ahp_xc_get_nlines())*sizeof(INumber)+1));
    snoopTelescopeNP = static_cast<INumberVectorProperty*>(realloc(snoopTelescopeNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    snoopTelescopeInfoN = static_cast<INumber*>(realloc(snoopTelescopeInfoN, static_cast<unsigned long>(4*ahp_xc_get_nlines())*sizeof(INumber)+1));
    snoopTelescopeInfoNP = static_cast<INumberVectorProperty*>(realloc(snoopTelescopeInfoNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    snoopDomeN = static_cast<INumber*>(realloc(snoopDomeN, static_cast<unsigned long>(2*ahp_xc_get_nlines())*sizeof(INumber)+1));
    snoopDomeNP = static_cast<INumberVectorProperty*>(realloc(snoopDomeNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    lineDelayN = static_cast<INumber*>(realloc(lineDelayN, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumber)+1));
    lineDelayNP = static_cast<INumberVectorProperty*>(realloc(lineDelayNP, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(INumberVectorProperty)+1));

    correlationsN = static_cast<INumber*>(realloc(correlationsN, static_cast<unsigned long>(2*ahp_xc_get_nlines()*(ahp_xc_get_nlines()-1)/2)*sizeof(INumber)+1));

    totalcounts = static_cast<double*>(realloc(totalcounts, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double)+1));
    totalcorrelations = static_cast<double*>(realloc(totalcorrelations, static_cast<unsigned long>(ahp_xc_get_nbaselines())*sizeof(double)+1));
    alt = static_cast<double*>(realloc(alt, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double)+1));
    az = static_cast<double*>(realloc(az, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double)+1));
    delay = static_cast<double*>(realloc(delay, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double)+1));
    baselines = static_cast<baseline**>(realloc(baselines, static_cast<unsigned long>(ahp_xc_get_nbaselines())*sizeof(baseline*)+1));

    memset (totalcounts, 0, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double)+1);
    memset (totalcorrelations, 0, static_cast<unsigned long>(ahp_xc_get_nbaselines())*sizeof(double)+1);
    memset (alt, 0, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double)+1);
    memset (az, 0, static_cast<unsigned long>(ahp_xc_get_nlines())*sizeof(double)+1);

    for(int x = 0; x < ahp_xc_get_nbaselines(); x++) {
        baselines[x] = new baseline();
        baselines[x]->initProperties();
    }

    int idx = 0;
    char tab[MAXINDINAME];
    char name[MAXINDINAME];
    char label[MAXINDINAME];
    for (int x = 0; x < ahp_xc_get_nlines(); x++) {
        //snoop properties
        IUFillNumber(&snoopTelescopeN[x*2+0], "RA", "RA (hh:mm:ss)", "%010.6m", 0, 24, 0, 0);
        IUFillNumber(&snoopTelescopeN[x*2+1], "DEC", "DEC (dd:mm:ss)", "%010.6m", -90, 90, 0, 0);

        IUFillNumber(&snoopTelescopeInfoN[x*4+0], "TELESCOPE_APERTURE", "Aperture (mm)", "%g", 10, 5000, 0, 0.0);
        IUFillNumber(&snoopTelescopeInfoN[x*4+1], "TELESCOPE_FOCAL_LENGTH", "Focal Length (mm)", "%g", 10, 10000, 0, 0.0);
        IUFillNumber(&snoopTelescopeInfoN[x*4+2], "GUIDER_APERTURE", "Guider Aperture (mm)", "%g", 10, 5000, 0, 0.0);
        IUFillNumber(&snoopTelescopeInfoN[x*4+3], "GUIDER_FOCAL_LENGTH", "Guider Focal Length (mm)", "%g", 10, 10000, 0, 0.0);

        IUFillNumber(&snoopGPSN[x*3+0], "LAT", "Lat (dd:mm:ss)", "%010.6m", -90, 90, 0, 0.0);
        IUFillNumber(&snoopGPSN[x*3+1], "LONG", "Lon (dd:mm:ss)", "%010.6m", 0, 360, 0, 0.0);
        IUFillNumber(&snoopGPSN[x*3+2], "ELEV", "Elevation (m)", "%g", -200, 10000, 0, 0);

        IUFillNumber(&lineDelayN[x], "DELAY", "Delay (m)", "%g", 0, EARTHRADIUSMEAN, 1.0E-9, 0);

        IUFillNumberVector(&snoopGPSNP[x], &snoopGPSN[x*3], 3, getDeviceName(), "GEOGRAPHIC_COORD", "Location", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);
        IUFillNumberVector(&snoopTelescopeNP[x], &snoopTelescopeN[x*2], 2, getDeviceName(), "EQUATORIAL_EOD_COORD", "Target coordinates", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);
        IUFillNumberVector(&snoopTelescopeInfoNP[x], &snoopTelescopeInfoN[x*4], 4, getDeviceName(), "TELESCOPE_INFO", "Scope Properties", OPTIONS_TAB, IP_RW, 60, IPS_OK);

        lineDevicesT[x*3+0].text = static_cast<char*>(malloc(1));
        IUFillText(&lineDevicesT[x*3+0], "ACTIVE_TELESCOPE", "Telescope", "Telescope Simulator");
        lineDevicesT[x*3+1].text = static_cast<char*>(malloc(1));
        IUFillText(&lineDevicesT[x*3+1], "ACTIVE_GPS", "GPS", "GPS Simulator");
        lineDevicesT[x*3+2].text = static_cast<char*>(malloc(1));
        IUFillText(&lineDevicesT[x*3+2], "ACTIVE_DOME", "DOME", "Dome Simulator");

        //interferometer properties
        IUFillNumber(&lineTelescopeN[x*4+0], "RA", "RA (hh:mm:ss)", "%010.6m", 0, 24, 0, 0);
        IUFillNumber(&lineTelescopeN[x*4+1], "DEC", "DEC (dd:mm:ss)", "%010.6m", -90, 90, 0, 0);
        IUFillNumber(&lineTelescopeN[x*4+2], "TELESCOPE_APERTURE", "Aperture (mm)", "%g", 10, 5000, 0, 0.0);
        IUFillNumber(&lineTelescopeN[x*4+3], "TELESCOPE_FOCAL_LENGTH", "Focal Length (mm)", "%g", 10, 10000, 0, 0.0);

        IUFillNumber(&lineGPSN[x*3+0], "LAT", "Lat (dd:mm:ss)", "%010.6m", -90, 90, 0, 0.0);
        IUFillNumber(&lineGPSN[x*3+1], "LONG", "Lon (dd:mm:ss)", "%010.6m", 0, 360, 0, 0.0);
        IUFillNumber(&lineGPSN[x*3+2], "ELEV", "Elevation (m)", "%g", -200, 10000, 0, 0);

        IUFillSwitch(&lineEnableS[x*2+0], "LINE_ENABLE", "Enable", ISS_OFF);
        IUFillSwitch(&lineEnableS[x*2+1], "LINE_DISABLE", "Disable", ISS_ON);

        IUFillSwitch(&linePowerS[x*2+0], "LINE_POWER_ON", "On", ISS_OFF);
        IUFillSwitch(&linePowerS[x*2+1], "LINE_POWER_OFF", "Off", ISS_ON);

        //report pulse counts
        IUFillNumber(&lineStatsN[x*4+0], "LINE_COUNTS", "Counts", "%g", 0.0, 400000000.0, 1.0, 0);
        IUFillNumber(&lineStatsN[x*4+1], "LINE_FLUX", "Photon Flux (Lm)", "%g", 0.0, 1.0, 1.0E-5, 0);
        IUFillNumber(&lineStatsN[x*4+2], "LINE_FLUX0", "Flux at mag0 (Lm)", "%g", 0.0, 1.0, 1.0E-5, 0);
        IUFillNumber(&lineStatsN[x*4+3], "LINE_MAGNITUDE", "Estimated magnitude", "%g", -22.0, 22.0, 1.0E-5, 0);

        sprintf(tab, "Line %02d", x+1);
        sprintf(name, "LINE_ENABLE_%02d", x+1);
        IUFillSwitchVector(&lineEnableSP[x], &lineEnableS[x*2], 2, getDeviceName(), name, "Enable Line", tab, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
        sprintf(name, "LINE_POWER_%02d", x+1);
        IUFillSwitchVector(&linePowerSP[x], &linePowerS[x*2], 2, getDeviceName(), name, "Power", tab, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
        sprintf(name, "LINE_SNOOP_DEVICES_%02d", x+1);
        IUFillTextVector(&lineDevicesTP[x], &lineDevicesT[x*3], 3, getDeviceName(), name, "Locator devices", tab, IP_RW, 60, IPS_IDLE);
        sprintf(name, "LINE_GEOGRAPHIC_COORD_%02d", x+1);
        IUFillNumberVector(&lineGPSNP[x], &lineGPSN[x*3], 3, getDeviceName(), name, "Location", tab, IP_RO, 60, IPS_IDLE);
        sprintf(name, "TELESCOPE_INFO_%02d", x+1);
        IUFillNumberVector(&lineTelescopeNP[x], &lineTelescopeN[x*4], 4, getDeviceName(), name, "Target coordinates", tab, IP_RO, 60, IPS_IDLE);
        sprintf(name, "LINE_DELAY_%02d", x+1);
        IUFillNumberVector(&lineDelayNP[x], &lineDelayN[x], 1, getDeviceName(), name, "Delay line", tab, IP_RO, 60, IPS_IDLE);
        sprintf(name, "LINE_STATS_%02d", x+1);
        IUFillNumberVector(&lineStatsNP[x], &lineStatsN[x*4], 4, getDeviceName(), name, "Stats", tab, IP_RO, 60, IPS_BUSY);
        for (int y = x+1; y < ahp_xc_get_nlines(); y++) {
            sprintf(name, "CORRELATIONS_%0d_%0d", x+1, y+1);
            sprintf(label, "Correlations %d*%d", x+1, y+1);
            IUFillNumber(&correlationsN[idx++], name, label, "%8.0f", 0, 400000000, 1, 0);
            sprintf(name, "COHERENCE_%0d_%0d", x+1, y+1);
            sprintf(label, "Coherence ratio (%d*%d)/(%d+%d)", x+1, y+1, x+1, y+1);
            IUFillNumber(&correlationsN[idx++], name, label, "%1.4f", 0, 1.0, 1, 0);
        }
    }
    IUFillNumberVector(&correlationsNP, correlationsN, ahp_xc_get_nbaselines()*2, getDeviceName(), "CORRELATIONS", "Correlations", "Stats", IP_RO, 60, IPS_BUSY);

    readThread = new std::thread(&AHP_XC::Callback, this);
    // Start the timer
    SetTimer(POLLMS);
    return true;
}

void AHP_XC::ActiveLine(int line, bool on, bool power)
{
    ahp_xc_set_leds(line, (on?1:0)|(power?2:0));
}

void AHP_XC::SetFrequencyDivider(unsigned char divider)
{
    ahp_xc_set_frequency_divider(divider);
}

void AHP_XC::EnableCapture(bool start)
{
    ahp_xc_enable_capture(start);
}
