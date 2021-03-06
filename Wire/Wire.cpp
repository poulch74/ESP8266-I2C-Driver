/**************************************************************************************/
/*
  TwoWire.cpp - TWI/I2C library for esp8266 Arduino & Wiring

  Modified          2012 by Todd Krein (todd@krein.org) to implement repeated starts
  Modified December 2014 by Ivan Grokhotkov (ivan@esp8266.com) - esp8266 support
  Modified April    2015 by Hrsto Gochkov (ficeto@ficeto.com) - alternative esp8266 support
  Modified October  2017 by enjoyneering79, sourse code: https://github.com/enjoyneering/

  This library is software/bit-bang emulation of Master I2C bus protocol, specials pins
  are required to interface. Connect slave to pins:

  Board:                                     SDA        SCL        Level
  ESP8266................................... GPIO4      GPIO5      3.3v/5v
  ESP8266 ESP-01............................ GPIO0/D5   GPIO2/D3   3.3v/5v
  NodeMCU 1.0, WeMos D1 Mini................ GPIO4/D2   GPIO5/D1   3.3v/5v

  NOTE:
  - I2C bus drivers are "open drain", meaning that they can pull the
    corresponding signal line low, but cannot drive it high. Thus, there can
    be no bus contention where one device is trying to drive the line high
    while another tries to pull it low, eliminating the potential for damage
    to the drivers or excessive power dissipation in the system. Each signal
    line has a pull-up resistor on it, to restore the signal to high when no
    device is asserting it low.

  Copyright (c) 2006 Nicholas Zambetti. All right reserved.
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/**************************************************************************************/

extern "C"
{
  #include <stdlib.h>
  #include <string.h>
  #include <inttypes.h>
}

#include "twi.h"   //i2c software bitbang emulation
#include "Wire.h"  //arduino wrapper

/**************************************************************************/
/*
    Initialize Class Variables
*/
/**************************************************************************/
uint8_t TwoWire::_rxBuffer[TWI_I2C_BUFFER_LENGTH];
uint8_t TwoWire::_rxBufferIndex  = 0;
uint8_t TwoWire::_rxBufferLength = 0;

uint8_t TwoWire::_txBuffer[TWI_I2C_BUFFER_LENGTH];
uint8_t TwoWire::_txAddress      = 0;
uint8_t TwoWire::_txBufferIndex  = 0;
uint8_t TwoWire::_txBufferLength = 0;
bool    TwoWire::_transmitting   = false;

void (*TwoWire::user_onRequest)(void);
void (*TwoWire::user_onReceive)(int);

static uint8_t default_sda_pin = SDA;
static uint8_t default_scl_pin = SCL;

/**************************************************************************/
/*
    Constructors
*/
/**************************************************************************/
TwoWire::TwoWire()
{
}

/**************************************************************************/
/*
    pins(), public method

    Deprecated, use begin(sda, scl) in new projects
*/
/**************************************************************************/
void TwoWire::pins(int sda, int scl)
{
  default_sda_pin = sda;
  default_scl_pin = scl;
}

/**************************************************************************/
/*
    begin(), public method

    Sets bus speed, clock strech limit & sets pins to open drain & switch
    them to high state

    NOTE:
    - I2C bus drivers are “open drain”, meaning that they can pull the
      corresponding signal line low, but cannot drive it high. Thus, there can
      be no bus contention where one device is trying to drive the line high
      while another tries to pull it low, eliminating the potential for damage
      to the drivers or excessive power dissipation in the system. Each signal
      line has a pull-up resistor on it, to restore the signal to high when no
      device is asserting it low.
*/
/**************************************************************************/
void TwoWire::begin(uint8_t sda, uint8_t scl)
{
  default_sda_pin = sda;
  default_scl_pin = scl;

  twi_init(sda, scl);
  flush();
}

/**************************************************************************/
/*
    begin(), public method

    Sets bus speed, clock strech limit & sets DEFAULT pins to open drain
    & switch them to high state
*/
/**************************************************************************/
void TwoWire::begin(void)
{
  begin(default_sda_pin, default_scl_pin);
}

/**************************************************************************/
/*
    setClock(), public method

    Sets I2C speed in Hz

    NOTE:
    - speed @ 80Mhz  CPU: 100kHz..400KHz with 100kHz step
    - speed @ 160Mhz CPU: 100kHz..700KHz with 100kHz step
    - 100KHz by default, real ~111kHz @ 80Mhz CPU
*/
/**************************************************************************/
void TwoWire::setClock(uint32_t frequency)
{
  twi_setClock(frequency);
}

/**************************************************************************/
/*
    setClockStretchLimit(), public method

    Sets SCL stretch limit, 230 by default 
*/
/**************************************************************************/
void TwoWire::setClockStretchLimit(uint32_t limit)
{
  twi_setClockStretchLimit(limit);
}

/**************************************************************************/
/*
    beginTransmission(), public method

    Sets all rx buffer variebles before reading
*/
/**************************************************************************/
void TwoWire::beginTransmission(uint8_t address)
{
  _txAddress      = address;
  _rxBufferIndex  = 0;
  _rxBufferLength = 0;
  _transmitting   = true;
}

void TwoWire::beginTransmission(int address)
{
  beginTransmission(static_cast<uint8_t>(address));
}

/**************************************************************************/
/*
    write(), public method

    Adds outcoming ONE byte into tx buffer for future transmit
    Returns the qnt of added bytes, in our case 1 or 0
*/
/**************************************************************************/
size_t TwoWire::write(uint8_t data)
{
  if (_transmitting == true)
  {
    if (_txBufferLength >= TWI_I2C_BUFFER_LENGTH)
    {
      setWriteError();
      return 0;
    }
    /* put one byte into the tx buffer */
    _txBuffer[_txBufferIndex] = data;
    _txBufferIndex++;
    _txBufferLength = _txBufferIndex;
  }
  else return 0;

  return 1;
}

/**************************************************************************/
/*
    write(), public method

    Adds outcoming ARRAY of bytes into tx buffer for future transmit
    Returns the qnt of successfully added bytes
*/
/**************************************************************************/
size_t TwoWire::write(const uint8_t *buffer, size_t quantity)
{
  for(size_t i = 0; i < quantity; i++)
  {
    if (write(buffer[i]) != 1) return i; //put the byte from array into the tx buffer
  }
  return quantity;
}

/**************************************************************************/
/*
    endTransmission(), public method

    Transmits the data from rx buffer to slave.

    Returns the i2c bus state:
    0 - success
    1 - data too long to fit in transmit buffer
    2 - received NACK on transmit of address
    3 - received NACK on transmit of data
    4 - can't start, line busy

    NOTE:
    - byte is transferred with the most significant bit (MSB) first
    - when master sets SDA HIGH during this 9-th clock pulse, this is
      defined as NACK (Not Acknowledge). The master generate STOP condition
      to abort the transfer
    - if master sets SDA LOW during this 9-th clock pulse, this is defined
      as ACK (Acknowledge). The master generate REPEATE START condition to
      start a new transfer.
    - regardless of the number of start conditions during one transfer
      the transfer must be ended by exactly one stop condition followed
      by NACK.
*/
/**************************************************************************/
uint8_t TwoWire::endTransmission(bool sendStop)
{
  if (_txBufferLength >= TWI_I2C_BUFFER_LENGTH) return 1;

  int8_t reply = twi_writeTo(_txAddress, _txBuffer, _txBufferLength, sendStop);

  flushTX();

  return reply;
}

uint8_t TwoWire::endTransmission(void)
{
  return endTransmission(true);
}

/**************************************************************************/
/*
    requestFrom(), public method
    
    Reads the data from slave to the rx buffer & returns the quantity
    of successfully received bytes
*/
/**************************************************************************/
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, bool sendStop)
{
  flushRX();                                                                                //do we need it here???

  if (quantity == 0 || quantity >= TWI_I2C_BUFFER_LENGTH) quantity = TWI_I2C_BUFFER_LENGTH; //safety check

  uint8_t length = twi_readFrom(address, _rxBuffer, quantity, sendStop);

  //_rxBufferIndex  = 0;                                                                    //this vs flushRX()??? 
  _rxBufferLength = length;

  return length;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop)
{
  return requestFrom(address, quantity, static_cast<bool>(sendStop));
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity)
{
  return requestFrom(address, quantity, true);
}

uint8_t TwoWire::requestFrom(int address, int quantity,  bool sendStop)
{
  return requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(quantity), sendStop);
}

uint8_t TwoWire::requestFrom(int address, int quantity, int sendStop)
{
  return requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(quantity), static_cast<bool>(sendStop));
}

uint8_t TwoWire::requestFrom(int address, int quantity)
{
  return requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(quantity), true);
}

/**************************************************************************/
/*
    available(), public method

    Checks the rx buffer & returns available qnt of bytes retrieved from slave 

    NOTE:
    - the proper way to use available()

      do
      {
        Wire.requestFrom(ADDRESS, 1, true); //read slave & fill the rxbuffer with data
      }
      while (Wire.available() != 1);        //check available data size in rxbuffer
*/
/**************************************************************************/
int TwoWire::available(void)
{
  int result = _rxBufferLength - _rxBufferIndex;

  /*
     yield will not make more data available but it will prevent the esp8266
     from going into WDT reset during the "while()"
  */
  if (result == 0) optimistic_yield(1000);

  return result;
}

/**************************************************************************/
/*
    read(), public method

    Returns byte value from rx buffer
*/
/**************************************************************************/
int TwoWire::read(void)
{
  int value = 0;

  if (_rxBufferIndex < _rxBufferLength)
  {
    value = _rxBuffer[_rxBufferIndex];
    _rxBufferIndex++;
  }
  return value;
}


/**************************************************************************/
/*
    peek(), public method

    Returns the value of last received byte without removing it
    from rx buffer.
*/
/**************************************************************************/
int TwoWire::peek(void)
{
  int value = 0;

  if (_rxBufferIndex < _rxBufferLength) value = _rxBuffer[_rxBufferIndex];

  return value;
}

/**************************************************************************/
/*
    flush(), public method

    Clears & resets rx/tx buffers
*/
/**************************************************************************/
void TwoWire::flush(void)
{
  flushTX();
  flushRX();

  /*
  _rxBufferIndex  = 0;
  _rxBufferLength = 0;

  _rxBufferIndex  = 0;
  _rxBufferLength = 0;
 */
}

/**************************************************************************/
/*
    status(), public method

    Returns i2c bus status
*/
/**************************************************************************/
uint8_t TwoWire::status()
{
  return twi_status();
}

/**************************************************************************/
/*
    onReceive(), public method
*/
/**************************************************************************/
void TwoWire::onReceive(void (*function)(int))
{
  (void)function;
  //user_onReceive = function;
}

/**************************************************************************/
/*
    onRequest(), public method
*/
/**************************************************************************/
void TwoWire::onRequest(void (*function)(void))
{
  (void)function;
  //user_onRequest = function;
}


/**************************************************************************/
/*
    flushRX(), private method

    Clears & resets rx buffers
*/
/**************************************************************************/
void TwoWire::flushRX(void)
{
  _rxBufferIndex  = 0;
  _rxBufferLength = 0;
  //memset(_rxBuffer, 0, TWI_I2C_BUFFER_LENGTH); //sets all bytes in the buffer to "0"
}

/**************************************************************************/
/*
    flushTX(), private method

    Clears & resets tx buffers
*/
/**************************************************************************/
void TwoWire::flushTX(void)
{
  _txBufferIndex  = 0;
  _txBufferLength = 0;
  _transmitting   = false;                     //ready to transmit flag
  //memset(_txBuffer, 0, TWI_I2C_BUFFER_LENGTH); //sets all bytes in the buffer to "0"
}

/**************************************************************************/
/*
    onReceiveService(), private method
*/
/**************************************************************************/
void TwoWire::onReceiveService(uint8_t* inBytes, int numBytes)
{
  (void)inBytes;
  (void)numBytes;
  /*
  //don't bother if user hasn't registered a callback
  if (!user_onReceive) return;

  //don't bother if rx buffer is in use by a master requestFrom() op
  //i know this drops data, but it allows for slight stupidity
  //meaning, they may not have read all the master requestFrom() data yet
  if (_rxBufferIndex < _rxBufferLength) return;

  //copy twi rx buffer into local read buffer
  //this enables new reads to happen in parallel
  for(uint8_t i = 0; i < numBytes; ++i)
  {
     _rxBuffer[i] = inBytes[i];
  }
  //set rx iterator vars
  _rxBufferIndex = 0;
  _rxBufferLength = numBytes;

  //alert user program
  user_onReceive(numBytes);
  */
}

/**************************************************************************/
/*
    onRequestService(), private method
*/
/**************************************************************************/
void TwoWire::onRequestService(void)
{
  /*
  //don't bother if user hasn't registered a callback
  if (!user_onRequest) return;

  //reset tx buffer iterator vars
  //!!! this will kill any pending pre-master sendTo() activity
  _rxBufferIndex  = 0;
  _rxBufferLength = 0;

  //alert user program
  user_onRequest();
  */
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_TWOWIRE)
TwoWire Wire;
#endif
