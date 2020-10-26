//******************************************************************************************
//*  Esp_radio -- Webradio receiver for ESP8266, (color) display and VS1053 MP3 module,    *

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <Ticker.h>
#include <stdio.h>
#include <string.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager    To avoid hardcoding wifi credentials


// Digital I/O used
// Pins for VS1053 module
#define VS1053_CS     D1
#define VS1053_DCS    D0
#define VS1053_DREQ   D3

// Ringbuffer for smooth playing. 20000 bytes is 160 Kbits, about 1.5 seconds at 128kb bitrate.
#define RINGBFSIZ 20000
// Debug buffer size
#define DEBUG_BUFFER_SIZE 100
// Name of the ini file
#define NAME "Esp-radio"
// Maximum number of MQTT reconnects before give-up
//
//******************************************************************************************
// Forward declaration of various functions                                                *
//******************************************************************************************
void   handlebyte ( uint8_t b, bool force = false ) ;
char*  dbgprint( const char* format, ... ) ;
bool   connecttohost() ;


//
//******************************************************************************************
// Global data section.                                                                    *
//******************************************************************************************
// There is a block ini-data that contains some configuration.  Configuration data is      *
// saved in the SPIFFS file radio.ini by the webinterface.  On restart the new data will   *
// be read from this file.                                                                 *
// Items in ini_block can be changed by commands from webserver/MQTT/Serial.               *
//******************************************************************************************


enum datamode_t { INIT = 1, HEADER = 2, DATA = 4,
                  METADATA = 8, PLAYLISTINIT = 16,
                  PLAYLISTHEADER = 32, PLAYLISTDATA = 64,
                  STOPREQD = 128, STOPPED = 256
                } ;        // State for datastream

// Global variables

int              DEBUG = 1 ;
WiFiClient       *mp3client = NULL ;                       // An instance of the mp3 client
Ticker           tckr ;                                    // For timing 100 msec
uint32_t         totalcount = 0 ;                          // Counter mp3 data
datamode_t       datamode ;                                // State of datastream
int              metacount ;                               // Number of bytes in metadata
int              datacount ;                               // Counter databytes before metadata
String           metaline ;                                // Readable line in metadata
String           icystreamtitle ;                          // Streamtitle from metadata
String           icyname ;                                 // Icecast station name
int              bitrate ;                                 // Bitrate in kb/sec
int              metaint = 0 ;                             // Number of databytes between metadata
int8_t           currentpreset = -1 ;                      // Preset station playing
String           host ;                                    // The URL to connect to or file to play
String           playlist ;                                // The URL of the specified playlist
bool             xmlreq = false ;                          // Request for XML parse.
bool             hostreq = false ;                         // Request for new host
bool             reqtone = false ;                         // New tone setting requested
bool             muteflag = false ;                        // Mute output
uint8_t*         ringbuf ;                                 // Ringbuffer for VS1053
uint16_t         rbwindex = 0 ;                            // Fill pointer in ringbuffer
uint16_t         rbrindex = RINGBFSIZ - 1 ;                // Emptypointer in ringbuffer
uint16_t         rcount = 0 ;                              // Number of bytes in ringbuffer
bool             resetreq = false ;                        // Request to reset the ESP8266
bool             localfile = false ;                       // Play from local mp3-file or not
bool             chunked = false ;                         // Station provides chunked transfer
int              chunkcount = 0 ;                          // Counter for chunked transfer

String      stationServer( "" ) ;                          // Radio stream server
String      stationPort( "" ) ;                            // Radio stream port
String      stationMount( "" ) ;                           // Radio stream Callsign

int         port = 80 ;                           // Port number for host
String      extension = "/franceinter-lofi.mp3" ;                     // May be like "/mp3" in "skonto.ls.lv:8002/mp3"
String      hostwoext = "icecast.radiofrance.fr" ;                           // Host without extension and portnumber

//https://icecast.radiofrance.fr/fipbordeaux-lofi.mp3?id=radiofrance


//******************************************************************************************
// End of global data section.                                                             *
//******************************************************************************************


//******************************************************************************************
// VS1053 stuff.  Based on maniacbug library.                                              *
//******************************************************************************************
// VS1053 class definition.                                                                *
//******************************************************************************************
class VS1053
{
  private:
    uint8_t       cs_pin ;                        // Pin where CS line is connected
    uint8_t       dcs_pin ;                       // Pin where DCS line is connected
    uint8_t       dreq_pin ;                      // Pin where DREQ line is connected
    uint8_t       curvol ;                        // Current volume setting 0..100%
    const uint8_t vs1053_chunk_size = 32 ;
    // SCI Register
    const uint8_t SCI_MODE          = 0x0 ;
    const uint8_t SCI_BASS          = 0x2 ;
    const uint8_t SCI_CLOCKF        = 0x3 ;
    const uint8_t SCI_AUDATA        = 0x5 ;
    const uint8_t SCI_WRAM          = 0x6 ;
    const uint8_t SCI_WRAMADDR      = 0x7 ;
    const uint8_t SCI_AIADDR        = 0xA ;
    const uint8_t SCI_VOL           = 0xB ;
    const uint8_t SCI_AICTRL0       = 0xC ;
    const uint8_t SCI_AICTRL1       = 0xD ;
    const uint8_t SCI_num_registers = 0xF ;
    // SCI_MODE bits
    const uint8_t SM_SDINEW         = 11 ;        // Bitnumber in SCI_MODE always on
    const uint8_t SM_RESET          = 2 ;         // Bitnumber in SCI_MODE soft reset
    const uint8_t SM_CANCEL         = 3 ;         // Bitnumber in SCI_MODE cancel song
    const uint8_t SM_TESTS          = 5 ;         // Bitnumber in SCI_MODE for tests
    const uint8_t SM_LINE1          = 14 ;        // Bitnumber in SCI_MODE for Line input
    SPISettings   VS1053_SPI ;                    // SPI settings for this slave
    uint8_t       endFillByte ;                   // Byte to send when stopping song
  protected:
    inline void await_data_request() const
    {
      while ( !digitalRead ( dreq_pin ) )
      {
        yield() ;                                 // Very short delay
      }
    }

    inline void control_mode_on() const
    {
      SPI.beginTransaction ( VS1053_SPI ) ;       // Prevent other SPI users
      digitalWrite ( dcs_pin, HIGH ) ;            // Bring slave in control mode
      digitalWrite ( cs_pin, LOW ) ;
    }

    inline void control_mode_off() const
    {
      digitalWrite ( cs_pin, HIGH ) ;             // End control mode
      SPI.endTransaction() ;                      // Allow other SPI users
    }

    inline void data_mode_on() const
    {
      SPI.beginTransaction ( VS1053_SPI ) ;       // Prevent other SPI users
      digitalWrite ( cs_pin, HIGH ) ;             // Bring slave in data mode
      digitalWrite ( dcs_pin, LOW ) ;
    }

    inline void data_mode_off() const
    {
      digitalWrite ( dcs_pin, HIGH ) ;            // End data mode
      SPI.endTransaction() ;                      // Allow other SPI users
    }

    uint16_t read_register ( uint8_t _reg ) const ;
    void     write_register ( uint8_t _reg, uint16_t _value ) const ;
    void     sdi_send_buffer ( uint8_t* data, size_t len ) ;
    void     sdi_send_fillers ( size_t length ) ;
    void     wram_write ( uint16_t address, uint16_t data ) ;
    uint16_t wram_read ( uint16_t address ) ;

  public:
    // Constructor.  Only sets pin values.  Doesn't touch the chip.  Be sure to call begin()!
    VS1053 ( uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin ) ;
    void     begin() ;                                   // Begin operation.  Sets pins correctly,
    // and prepares SPI bus.
    void     startSong() ;                               // Prepare to start playing. Call this each
    // time a new song starts.
    void     playChunk ( uint8_t* data, size_t len ) ;   // Play a chunk of data.  Copies the data to
    // the chip.  Blocks until complete.
    void     stopSong() ;                                // Finish playing a song. Call this after
    // the last playChunk call.
    void     setVolume ( uint8_t vol ) ;                 // Set the player volume.Level from 0-100,
    // higher is louder.
    void     setTone ( uint8_t* rtone ) ;                // Set the player baas/treble, 4 nibbles for
    // treble gain/freq and bass gain/freq
    uint8_t  getVolume() ;                               // Get the currenet volume setting.
    // higher is louder.
    void     printDetails ( const char *header ) ;       // Print configuration details to serial output.
    void     softReset() ;                               // Do a soft reset
    bool     testComm ( const char *header ) ;           // Test communication with module
    inline bool data_request() const
    {
      return ( digitalRead ( dreq_pin ) == HIGH ) ;
    }
    void     AdjustRate ( long ppm2 ) ;                  // Fine tune the datarate
} ;

//******************************************************************************************
// VS1053 class implementation.                                                            *
//******************************************************************************************

VS1053::VS1053 ( uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin ) :
  cs_pin(_cs_pin), dcs_pin(_dcs_pin), dreq_pin(_dreq_pin)
{
}

uint16_t VS1053::read_register ( uint8_t _reg ) const
{
  uint16_t result ;

  control_mode_on() ;
  SPI.write ( 3 ) ;                                // Read operation
  SPI.write ( _reg ) ;                             // Register to write (0..0xF)
  // Note: transfer16 does not seem to work
  result = ( SPI.transfer ( 0xFF ) << 8 ) |        // Read 16 bits data
           ( SPI.transfer ( 0xFF ) ) ;
  await_data_request() ;                           // Wait for DREQ to be HIGH again
  control_mode_off() ;
  return result ;
}

void VS1053::write_register ( uint8_t _reg, uint16_t _value ) const
{
  control_mode_on( );
  SPI.write ( 2 ) ;                                // Write operation
  SPI.write ( _reg ) ;                             // Register to write (0..0xF)
  SPI.write16 ( _value ) ;                         // Send 16 bits data
  await_data_request() ;
  control_mode_off() ;
}

void VS1053::sdi_send_buffer ( uint8_t* data, size_t len )
{
  size_t chunk_length ;                            // Length of chunk 32 byte or shorter

  data_mode_on() ;
  while ( len )                                    // More to do?
  {
    await_data_request() ;                         // Wait for space available
    chunk_length = len ;
    if ( len > vs1053_chunk_size )
    {
      chunk_length = vs1053_chunk_size ;
    }
    len -= chunk_length ;
    SPI.writeBytes ( data, chunk_length ) ;
    data += chunk_length ;
  }
  data_mode_off() ;
}

void VS1053::sdi_send_fillers ( size_t len )
{
  size_t chunk_length ;                            // Length of chunk 32 byte or shorter

  data_mode_on() ;
  while ( len )                                    // More to do?
  {
    await_data_request() ;                         // Wait for space available
    chunk_length = len ;
    if ( len > vs1053_chunk_size )
    {
      chunk_length = vs1053_chunk_size ;
    }
    len -= chunk_length ;
    while ( chunk_length-- )
    {
      SPI.write ( endFillByte ) ;
    }
  }
  data_mode_off();
}

void VS1053::wram_write ( uint16_t address, uint16_t data )
{
  write_register ( SCI_WRAMADDR, address ) ;
  write_register ( SCI_WRAM, data ) ;
}

uint16_t VS1053::wram_read ( uint16_t address )
{
  write_register ( SCI_WRAMADDR, address ) ;            // Start reading from WRAM
  return read_register ( SCI_WRAM ) ;                   // Read back result
}

bool VS1053::testComm ( const char *header )
{
  // Test the communication with the VS1053 module.  The result wille be returned.
  // If DREQ is low, there is problably no VS1053 connected.  Pull the line HIGH
  // in order to prevent an endless loop waiting for this signal.  The rest of the
  // software will still work, but readbacks from VS1053 will fail.
  int       i ;                                         // Loop control
  uint16_t  r1, r2, cnt = 0 ;
  uint16_t  delta = 300 ;                               // 3 for fast SPI

  if ( !digitalRead ( dreq_pin ) )
  {
    dbgprint ( "VS1053 not properly installed!" ) ;
    // Allow testing without the VS1053 module
    pinMode ( dreq_pin,  INPUT_PULLUP ) ;               // DREQ is now input with pull-up
    return false ;                                      // Return bad result
  }
  // Further TESTING.  Check if SCI bus can write and read without errors.
  // We will use the volume setting for this.
  // Will give warnings on serial output if DEBUG is active.
  // A maximum of 20 errors will be reported.
  if ( strstr ( header, "Fast" ) )
  {
    delta = 3 ;                                         // Fast SPI, more loops
  }
  dbgprint ( header ) ;                                 // Show a header
  for ( i = 0 ; ( i < 0xFFFF ) && ( cnt < 20 ) ; i += delta )
  {
    write_register ( SCI_VOL, i ) ;                     // Write data to SCI_VOL
    r1 = read_register ( SCI_VOL ) ;                    // Read back for the first time
    r2 = read_register ( SCI_VOL ) ;                    // Read back a second time
    if  ( r1 != r2 || i != r1 || i != r2 )              // Check for 2 equal reads
    {
      dbgprint ( "VS1053 error retry SB:%04X R1:%04X R2:%04X", i, r1, r2 ) ;
      cnt++ ;
      delay ( 10 ) ;
    }
    yield() ;                                           // Allow ESP firmware to do some bookkeeping
  }
  return ( cnt == 0 ) ;                                 // Return the result
}

void VS1053::begin()
{
  pinMode      ( dreq_pin,  INPUT ) ;                   // DREQ is an input
  pinMode      ( cs_pin,    OUTPUT ) ;                  // The SCI and SDI signals
  pinMode      ( dcs_pin,   OUTPUT ) ;
  digitalWrite ( dcs_pin,   HIGH ) ;                    // Start HIGH for SCI en SDI
  digitalWrite ( cs_pin,    HIGH ) ;
  delay ( 100 ) ;
  dbgprint ( "Reset VS1053..." ) ;
  digitalWrite ( dcs_pin,   LOW ) ;                     // Low & Low will bring reset pin low
  digitalWrite ( cs_pin,    LOW ) ;
  delay ( 2000 ) ;
  dbgprint ( "End reset VS1053..." ) ;
  digitalWrite ( dcs_pin,   HIGH ) ;                    // Back to normal again
  digitalWrite ( cs_pin,    HIGH ) ;
  delay ( 500 ) ;
  // Init SPI in slow mode ( 0.2 MHz )
  VS1053_SPI = SPISettings ( 200000, MSBFIRST, SPI_MODE0 ) ;
  //printDetails ( "Right after reset/startup" ) ;
  delay ( 20 ) ;
  //printDetails ( "20 msec after reset" ) ;
  testComm ( "Slow SPI,Testing VS1053 read/write registers..." ) ;
  // Most VS1053 modules will start up in midi mode.  The result is that there is no audio
  // when playing MP3.  You can modify the board, but there is a more elegant way:
  wram_write ( 0xC017, 3 ) ;                            // GPIO DDR = 3
  wram_write ( 0xC019, 0 ) ;                            // GPIO ODATA = 0
  delay ( 100 ) ;
  //printDetails ( "After test loop" ) ;
  softReset() ;                                         // Do a soft reset
  // Switch on the analog parts
  write_register ( SCI_AUDATA, 44100 + 1 ) ;            // 44.1kHz + stereo
  // The next clocksetting allows SPI clocking at 5 MHz, 4 MHz is safe then.
  write_register ( SCI_CLOCKF, 6 << 12 ) ;              // Normal clock settings multiplyer 3.0 = 12.2 MHz
  //SPI Clock to 4 MHz. Now you can set high speed SPI clock.
  VS1053_SPI = SPISettings ( 4000000, MSBFIRST, SPI_MODE0 ) ;
  write_register ( SCI_MODE, _BV ( SM_SDINEW ) | _BV ( SM_LINE1 ) ) ;
  testComm ( "Fast SPI, Testing VS1053 read/write registers again..." ) ;
  delay ( 10 ) ;
  await_data_request() ;
  endFillByte = wram_read ( 0x1E06 ) & 0xFF ;
  dbgprint ( "endFillByte is %X", endFillByte ) ;
  //printDetails ( "After last clocksetting" ) ;
  delay ( 100 ) ;
}

void VS1053::setVolume ( uint8_t vol )
{
  // Set volume.  Both left and right.
  // Input value is 0..100.  100 is the loudest.
  // Clicking reduced by using 0xf8 to 0x00 as limits.
  uint16_t value ;                                      // Value to send to SCI_VOL

  if ( vol != curvol )
  {
    curvol = vol ;                                      // Save for later use
    value = map ( vol, 0, 100, 0xF8, 0x00 ) ;           // 0..100% to one channel
    value = ( value << 8 ) | value ;
    write_register ( SCI_VOL, value ) ;                 // Volume left and right
  }
}

void VS1053::setTone ( uint8_t *rtone )                 // Set bass/treble (4 nibbles)
{
  // Set tone characteristics.  See documentation for the 4 nibbles.
  uint16_t value = 0 ;                                  // Value to send to SCI_BASS
  int      i ;                                          // Loop control

  for ( i = 0 ; i < 4 ; i++ )
  {
    value = ( value << 4 ) | rtone[i] ;                 // Shift next nibble in
  }
  write_register ( SCI_BASS, value ) ;                  // Tone settings
  value = read_register ( SCI_BASS ) ;                  // Read back
  dbgprint ( "BASS settings is %04X", value ) ;         // Print for TEST
}

uint8_t VS1053::getVolume()                             // Get the currenet volume setting.
{
  return curvol ;
}

void VS1053::startSong()
{
  sdi_send_fillers ( 10 ) ;
}

void VS1053::playChunk ( uint8_t* data, size_t len )
{
  sdi_send_buffer ( data, len ) ;
}

void VS1053::stopSong()
{
  uint16_t modereg ;                     // Read from mode register
  int      i ;                           // Loop control

  sdi_send_fillers ( 2052 ) ;
  delay ( 10 ) ;
  write_register ( SCI_MODE, _BV ( SM_SDINEW ) | _BV ( SM_CANCEL ) ) ;
  for ( i = 0 ; i < 200 ; i++ )
  {
    sdi_send_fillers ( 32 ) ;
    modereg = read_register ( SCI_MODE ) ;  // Read status
    if ( ( modereg & _BV ( SM_CANCEL ) ) == 0 )
    {
      sdi_send_fillers ( 2052 ) ;
      dbgprint ( "Song stopped correctly after %d msec", i * 10 ) ;
      return ;
    }
    delay ( 10 ) ;
  }
  printDetails ( "Song stopped incorrectly!" ) ;
}

void VS1053::softReset()
{
  write_register ( SCI_MODE, _BV ( SM_SDINEW ) | _BV ( SM_RESET ) ) ;
  delay ( 10 ) ;
  await_data_request() ;
}

void VS1053::printDetails ( const char *header )
{
  uint16_t     regbuf[16] ;
  uint8_t      i ;

  dbgprint ( header ) ;
  dbgprint ( "REG   Contents" ) ;
  dbgprint ( "---   -----" ) ;
  for ( i = 0 ; i <= SCI_num_registers ; i++ )
  {
    regbuf[i] = read_register ( i ) ;
  }
  for ( i = 0 ; i <= SCI_num_registers ; i++ )
  {
    delay ( 5 ) ;
    dbgprint ( "%3X - %5X", i, regbuf[i] ) ;
  }
}

void VS1053::AdjustRate ( long ppm2 )
{
  write_register ( SCI_WRAMADDR, 0x1e07 ) ;
  write_register ( SCI_WRAM,     ppm2 ) ;
  write_register ( SCI_WRAM,     ppm2 >> 16 ) ;
  // oldClock4KHz = 0 forces  adjustment calculation when rate checked.
  write_register ( SCI_WRAMADDR, 0x5b1c ) ;
  write_register ( SCI_WRAM,     0 ) ;
  // Write to AUDATA or CLOCKF checks rate and recalculates adjustment.
  write_register ( SCI_AUDATA,   read_register ( SCI_AUDATA ) ) ;
}


// The object for the MP3 player
VS1053 vs1053player (  VS1053_CS, VS1053_DCS, VS1053_DREQ ) ;

//******************************************************************************************
// End VS1053 stuff.                                                                       *
//******************************************************************************************



//******************************************************************************************
// Ringbuffer (fifo) routines.                                                             *
//******************************************************************************************
//******************************************************************************************
//                              R I N G S P A C E                                          *
//******************************************************************************************
inline bool ringspace()
{
  return ( rcount < RINGBFSIZ ) ;     // True is at least one byte of free space is available
}


//******************************************************************************************
//                              R I N G A V A I L                                          *
//******************************************************************************************
inline uint16_t ringavail()
{
  return rcount ;                     // Return number of bytes available
}


//******************************************************************************************
//                                P U T R I N G                                            *
//******************************************************************************************
void putring ( uint8_t b )                 // Put one byte in the ringbuffer
{
  // No check on available space.  See ringspace()
  *(ringbuf + rbwindex) = b ;         // Put byte in ringbuffer
  if ( ++rbwindex == RINGBFSIZ )      // Increment pointer and
  {
    rbwindex = 0 ;                    // wrap at end
  }
  rcount++ ;                          // Count number of bytes in the
}


//******************************************************************************************
//                                G E T R I N G                                            *
//******************************************************************************************
uint8_t getring()
{
  // Assume there is always something in the bufferpace.  See ringavail()
  if ( ++rbrindex == RINGBFSIZ )      // Increment pointer and
  {
    rbrindex = 0 ;                    // wrap at end
  }
  rcount-- ;                          // Count is now one less
  return *(ringbuf + rbrindex) ;      // return the oldest byte
}

//******************************************************************************************
//                               E M P T Y R I N G                                         *
//******************************************************************************************
void emptyring()
{
  rbwindex = 0 ;                      // Reset ringbuffer administration
  rbrindex = RINGBFSIZ - 1 ;
  rcount = 0 ;
}



//******************************************************************************************
//                                  D B G P R I N T                                        *
//******************************************************************************************
// Send a line of info to serial output.  Works like vsprintf(), but checks the BEDUg flag.*
// Print only if DEBUG flag is true.  Always returns the the formatted string.             *
//******************************************************************************************
char* dbgprint ( const char* format, ... )
{
  static char sbuf[DEBUG_BUFFER_SIZE] ;                // For debug lines
  va_list varArgs ;                                    // For variable number of params

  va_start ( varArgs, format ) ;                       // Prepare parameters
  vsnprintf ( sbuf, sizeof(sbuf), format, varArgs ) ;  // Format the message
  va_end ( varArgs ) ;                                 // End of using parameters
  if ( DEBUG )                                         // DEBUG on?
  {
    Serial.print ( "D: " ) ;                           // Yes, print prefix
    Serial.println ( sbuf ) ;                          // and the info
  }
  return sbuf ;                                        // Return stored string
}





//******************************************************************************************
//                            S T O P _ M P 3 C L I E N T                                  *
//******************************************************************************************
// Disconnect from the server.                                                             *
//******************************************************************************************
void stop_mp3client ()
{
  if ( mp3client )
  {
    if ( mp3client->connected() )                    // Need to stop client?
    {
      dbgprint ( "Stopping client" ) ;               // Stop connection to host
      mp3client->flush() ;
      mp3client->stop() ;
      delay ( 500 ) ;
    }
    delete ( mp3client ) ;
    mp3client = NULL ;
  }
}


//******************************************************************************************
//                            C O N N E C T T O H O S T                                    *

//******************************************************************************************
// Connect to the Internet radio server specified by newpreset.                            *
//******************************************************************************************
bool connecttohost()
{
                    // Host without extension and portnumber

  stop_mp3client() ;                                // Disconnect if still connected
  dbgprint ( "Connect to new host %s", hostwoext.c_str() ) ;
  datamode = INIT ;                                 // Start default in metamode
  chunked = false ;                                 // Assume not chunked

  
  mp3client = new WiFiClient() ;
  if ( mp3client->connect ( hostwoext.c_str(), port ) )
  {
    dbgprint ( "Connected to server" ) ;
    // This will send the request to the server. Request metadata.
    mp3client->print ( String ( "GET " ) +
                       extension +
                       String ( " HTTP/1.1\r\n" ) +
                       String ( "Host: " ) +
                       hostwoext +
                       String ( "\r\n" ) +
                       String ( "Icy-MetaData:1\r\n" ) +
                       String ( "Connection: close\r\n\r\n" ) ) ;
    return true ;
  }
  dbgprint ( "Request %s failed!", hostwoext.c_str() ) ;
  return false ;
}

//******************************************************************************************
//                                   S E T U P                                             *
//******************************************************************************************
// Setup for the program.                                                                  *
//******************************************************************************************
void setup()
{

  Serial.begin ( 115200 ) ;                            // For debug
  Serial.println() ;
  //system_update_cpu_freq ( 160 ) ;                     // Set to 80/160 MHz
  WiFiManager wifiManager;
  wifiManager.autoConnect("Configure_webradio");
  Serial.println("Wifi Connected.");

  ringbuf = (uint8_t *) malloc ( RINGBFSIZ ) ;         // Create ring buffer
  SPI.begin() ;                                        // Init SPI bus
  
  
  vs1053player.begin() ;                               // Initialize VS1053 player
  vs1053player.setVolume(80);
  delay(10);
  connecttohost();
  datamode = DATA;
  
}


//******************************************************************************************
//                                   L O O P                                               *
//******************************************************************************************
// Main loop of the program.  Minimal time is 20 usec.  Will take about 4 msec if VS1053   *
// needs data.                                                                             *
// Sometimes the loop is called after an interval of more than 100 msec.                   *
// In that case we will not be able to fill the internal VS1053-fifo in time (especially   *
// at high bitrate).                                                                       *
// A connection to an MP3 server is active and we are ready to receive data.               *
// Normally there is about 2 to 4 kB available in the data stream.  This depends on the    *
// sender.                                                                                 *
//******************************************************************************************
void loop()
{
  uint32_t    maxfilechunk  ;                           // Max number of bytes to read from
  // stream or file
  // Try to keep the ringbuffer filled up by adding as much bytes as possible


  maxfilechunk = mp3client->available() ;          // Bytes available from mp3 server
  if ( maxfilechunk > 1024 )                       // Reduce byte count for this loop()
  {
    maxfilechunk = 1024 ;
  }
  while ( ringspace() && maxfilechunk-- )
  {
    putring ( mp3client->read() ) ;                // Yes, store one byte in ringbuffer
    yield() ;
  }

  yield() ;

  while ( vs1053player.data_request() && ringavail() ) // Try to keep VS1053 filled
  {
    handlebyte ( getring() ) ;                      // Yes, handle it
  }
  yield() ;


 
}






//******************************************************************************************
//                           H A N D L E B Y T E                                           *
//******************************************************************************************
// Handle the next byte of data from server.                                               *
// This byte will be send to the VS1053 most of the time.                                  *
// Note that the buffer the data chunk must start at an address that is a muttiple of 4.   *
// Set force to true if chunkbuffer must be flushed.                                       *
//******************************************************************************************
void handlebyte ( uint8_t b, bool force )
{
  static uint16_t  playlistcnt ;                       // Counter to find right entry in playlist
  static bool      firstmetabyte ;                     // True if first metabyte (counter)
  static int       LFcount ;                           // Detection of end of header
  static __attribute__((aligned(4))) uint8_t buf[32] ; // Buffer for chunk
  static int       bufcnt = 0 ;                        // Data in chunk
  static bool      firstchunk = true ;                 // First chunk as input
  String           lcml ;                              // Lower case metaline
  String           ct ;                                // Contents type
  static bool      ctseen = false ;                    // First line of header seen or not
  int              inx ;                               // Pointer in metaline
  int              i ;                                 // Loop control

  if ( datamode == INIT )                              // Initialize for header receive
  {
    ctseen = false ;                                   // Contents type not seen yet
    metaint = 0 ;                                      // No metaint found
    LFcount = 0 ;                                      // For detection end of header
    bitrate = 0 ;                                      // Bitrate still unknown
    
    totalcount = 0 ;                                   // Reset totalcount
    metaline = "" ;                                    // No metadata yet
    firstchunk = true ;                                // First chunk expected
  }
  if ( datamode == DATA )                              // Handle next byte of MP3/Ogg data
  {
    buf[bufcnt++] = b ;                                // Save byte in chunkbuffer
    if ( bufcnt == sizeof(buf) || force )              // Buffer full?
    {
      if ( firstchunk )
      {
        firstchunk = false ;
        dbgprint ( "First chunk:" ) ;                  // Header for printout of first chunk
        for ( i = 0 ; i < 32 ; i += 8 )                // Print 4 lines
        {
          dbgprint ( "%02X %02X %02X %02X %02X %02X %02X %02X",
                     buf[i],   buf[i + 1], buf[i + 2], buf[i + 3],
                     buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7] ) ;
        }
      }
      vs1053player.playChunk ( buf, bufcnt ) ;         // Yes, send to player
      bufcnt = 0 ;                                     // Reset count
    }
    totalcount++ ;                                     // Count number of bytes, ignore overflow
    if ( metaint != 0 )                                // No METADATA on Ogg streams or mp3 files
    {
      if ( --datacount == 0 )                          // End of datablock?
      {
        if ( bufcnt )                                  // Yes, still data in buffer?
        {
          vs1053player.playChunk ( buf, bufcnt ) ;     // Yes, send to player
          bufcnt = 0 ;                                 // Reset count
        }
        datamode = METADATA ;
        firstmetabyte = true ;                         // Expecting first metabyte (counter)
      }
    }
    return ;
  }
  
  
  
}
