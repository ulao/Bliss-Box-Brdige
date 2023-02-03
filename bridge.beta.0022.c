#include "conPad.h"
#include <util/delay.h>
#include "bridge.h"
#include "bridge_protocal.h"
#include "../shared/shared_functions.c"

int doMap; //using a time out sinnce a one time didnt work, TODO why??
Bridge_interface *getBridgeInterface(void);

//IRQ for consoles that get stuck or take a long time to poll, use as needed. 
ISR(TIMER1_COMPA_vect) 
{
	_CON_TIMER_EXP_FLAG = true;
	
		DDRC |= 0x40; PORTC &= !~0x40; _delay_us(2); PORTC |= 0x40;
		
	// even though this should be restting, its not. I need this here or the irq only fires once. 
	TCNT1 = 0; //do not remove without testing. 
	
	if (Console.Type == JAG) 
	{
		bridgeInterface->write(BRIDGE_CONTROLLER_POLL);//poll to keep BB happy, without this we get left over payloads. 
	}
	else if (Console.Type == GEN) 
	{
		//fake paulse so we stop waiting. 
		if (PINC & 0x40) PORTC &= ~0x40;
		else PORTC  |= 0x40;
		DDRC |= 0x40;//push out. 
		
		//not thast this will end the loops in the Gen code thus making it chage the pins states. The only way to prevent this is a 
		//ISR in the gen code, or make the gen state global.
	}
	
	
}

void getInfo ( void )
{

	unsigned char rply=GET_CONTROLLER_STATUS_ERROR_AP_NO_REPORT;
	while(rply >= ERROR_START_VALUE)
	{
		rply=bridgeInterface->write(MODE_READ); 
		bridgeInterface->read(2);
		Controller.AutoPaused = (BB_ReadBuffer[1] & 0x01);//not reaslly needed. Err is 255 for AP, could  ues somethign else in its place. 
		//
		//
		Controller.HotSwapDisabled = (BB_ReadBuffer[1] & 0x08)? true:false;
		Controller.UDLR = (BB_ReadBuffer[1] & 0x10)? true:false;
		Controller.DisableAllCombos = (BB_ReadBuffer[1] & 0x20)? true:false;
		Controller.AutoPausedDisabled = (BB_ReadBuffer[1] & 0x40) ?true:false;
		Controller.DpadOnly = (BB_ReadBuffer[1] & 0x80)? true:false;
		//_delay_ms(1);//if a wait here is going to be needed, it will kill 5200m, very short window. 
		if (rply==GET_CONTROLLER_STATUS_ERROR_AP_NO_REPORT) bridgeInterface->write(BRIDGE_CONTROLLER_POLL);//prevents getting stuck
	}
}


void checkControllerType(void ) 
{
	if (	Controller.Type == 121 || //ds2
			Controller.Type == 51  || //PSX_NEGCON
			Controller.Type == 127    // PSX_JOGCON
		) 
	Controller.Pressure=true;
	else Controller.Pressure=false;
	
	
	if (	Controller.Type == 115 || //ds
			Controller.Type == 121 || //ds2
			Controller.Type == 51  || //PSX_NEGCON
			Controller.Type == 127 || // PSX_JOGCON
			Controller.Type == 9	  || //GC
			Controller.Type == 18  || //GC_WHEEL
			Controller.Type == 19  //N64
		) 		
	Controller.Rumble=true;
	else Controller.Rumble=false;
}

/////////////////////////////////
// LLAPI polling ralated code  //
// will return the first byte  //
// Typically controller iD, or //
// error                       //
/////////////////////////////////	
unsigned char do_LLAPI( void ) 
{
	unsigned char rply ;


	// in AP mode we will need a poll first or no detection takes place. Then we need to get the ID so a data read is needed. But normaly its best not to poll first
	if ( Controller.AutoPaused ) 
	{
		if ( bridgeInterface->write(BRIDGE_CONTROLLER_POLL) == -1) return GET_CONTROLLER_STATUS_ERROR_NOTREADY;//other wise proceed. 
	}

	///////////////
	//  Pressure //
	///////////////
	if ( Controller.Pressure  )
	{

		rply = GET_CONTROLLER_STATUS_ERROR_NOTREADY;
		bridgeInterface->write(BRDIGE_GET_PRESSURE_STATUS);
		rply =bridgeInterface->read(13);

		_delay_us(300);//without this we will get bad read from pressure to payloads, seem not to happen in revers? This is not undertood
		
		if (rply < ERROR_START_VALUE)
		{
			for (char i=1; i<13;i++) {pressureData[i-1]=BB_ReadBuffer[i];}
		} else return rply; //has errors so stop and return
	}

	///////////////
	//  Payload  //
	///////////////

	rply = GET_CONTROLLER_STATUS_ERROR_NOTREADY;
	bridgeInterface->write(BRIDGE_GET_CONTROLLER_STATUS);
	rply =bridgeInterface->read(BB_BUFFER_SIZE);

	if ( rply < ERROR_START_VALUE && Controller.Type  !=  rply)
	{
		//controller changed. 
		bridgeInterface->write(BRIDGE_CONTROLLER_POLL);//need to poll to get info updated.
		getInfo();
		
		//get fresh data ( note that in sega console mode this was causing a pressing of C button, the fix was to get fresh data)
		bridgeInterface->write(BRIDGE_GET_CONTROLLER_STATUS);
		rply =bridgeInterface->read(BB_BUFFER_SIZE);
		
		doMap = 255;

	}

	if (rply >= ERROR_START_VALUE)  return rply; //has errors so stop and return

	///////////////
	//  Rumble   //
	///////////////

	if ( Console.Rumble && Controller.Rumble )
	{
	
	char check = RumbleLargeMotor ^ RumbleSmallMotor; 
	
	//consolea use rumble as a on or off unlike PC. So we want to senes when a motor has a value
	//but only send an off when the value is first 0. Otherwise we just keep spamming the bus 
	//with off data. 
	
	 
	//if there is data or the state changed to off. 
	if (check || oldRumbleState != check )  
	{		
		//since setting a small motor will overright the large motor in the case of a single
		//motor, we need to use the small motor for this. Small motor will be set second so it 
		//overrights large. In the case of attached xbox controller, both will be used.
		//if we are running off 3v and we use a PSX controller simply using small is all we need. 
				 
			bridgeInterface->write(0x1c); //set effect parms
			bridgeInterface->write(RumbleLargeMotor);//level
			bridgeInterface->write((RumbleLargeMotor)?255:0);//255 is 4 seconds max , 0 to stop
			bridgeInterface->write(0x11);//play effect const

			bridgeInterface->write(0x1c); //set effect parms
			bridgeInterface->write(RumbleSmallMotor);//level
			bridgeInterface->write((RumbleSmallMotor)?255:0);//255 is 4 seconds max , 0 to stop
			bridgeInterface->write(0x14);//play effect sine
			
			//save a combine state
			oldRumbleState = RumbleLargeMotor ^ RumbleSmallMotor; 
		}
	}

	if ( doMap )
	{
		doMap --;
		con_autoMapper();
	}
	bridgeInterface->write(BRIDGE_CONTROLLER_POLL);//poll later while consol does its thing. 

	return rply;
}

/////////////////////////////////
// Runs the main code update   //
// logic. Monitors controller  //
// Changes from BB and Consol  //
/////////////////////////////////	
void main_update(void)
{ 
	unsigned char type =  do_LLAPI();
 	
	//only report in normal operation. 
	if (! Controller.AutoPaused ) //normal operation
	{
		if (type >= ERROR_START_VALUE)  //if we get any error 
		{
			if ( (type & BB_ReadBuffer[1] & BB_ReadBuffer[2] ) == 255 )  {} //all data is high case, this is not to be mistakin for AP, its bad data. 
			else if ( type==  GET_CONTROLLER_STATUS_ERROR_AP_NO_REPORT || type == LLAPI_BUSY_ERROR ) Controller.AutoPaused=true;
			else if ( type==GET_CONTROLLER_STATUS_ERROR_NOTREADY) {}//TODO do anythign here? or the same?
			
			Controller.DpadOnly = true; //this need to be reset here so that controller that always detect non digital , issue once dopad only first. (saturn code needs this)
	
			memcpy (BB_ReadBuffer,BACKUP_ReadBuffer,BB_BUFFER_SIZE); //restore from backup as we were makred as dirty.
			
			type = Controller.Type;// restore from last good type, because putting errors in the type won;t make any sense to the console
		}
		else //no error
		{
		
			memcpy (BACKUP_ReadBuffer,BB_ReadBuffer,BB_BUFFER_SIZE); //make a backup.
			Controller.Type = type; //type is good, set it and back it up.
		}
	} 
	else  
	{
		memcpy (BB_ReadBuffer,BACKUP_ReadBuffer,BB_BUFFER_SIZE); //restore from backup.
		if ( type < ERROR_START_VALUE) Controller.AutoPaused = false; //good read so clear ap. 
	}
	
	//auto pause
	if ( Controller.AutoPaused && !_autoPausePressed )
	{
		_autoPausePressed = true;
		BB_ReadBuffer[1] |= 0x20;//press start.  
	}
	else if ( Controller.AutoPaused &&  _autoPausePressed ) BB_ReadBuffer[1] &= ~0x20;//release start.  			
	else if( ! Controller.AutoPaused && _autoPausePressed) 
	{
		_autoPausePressed = false; //re-arm
		getInfo();
		doMap = 255;
	}
		
	//add this only to where it is needed 
	//mapDefaltCalibration();//note gc does some kind of its own calabration. This seem to only make it worse
	//mapDefultDeadZone();
	
	reply = type;//local to global forward, maybe consider using the global instead thriough out here?
				 //global needs to know the tyep some how. coudl remove all this global type stuff and pass it in?
				 // but it is handy to know the type globaly?
	
	globalMaps();

	con_clear();//not really needed but tidy.
			
	
	//global options and/or mappings here.
	reportBuffer[BUTTON_ROW_1]=BB_ReadBuffer[1];
	reportBuffer[BUTTON_ROW_2]=BB_ReadBuffer[2];
	reportBuffer[BUTTON_ROW_3]=BB_ReadBuffer[3];

	reportBuffer[X_MAIN_STICK]	=BB_ReadBuffer[4];
	reportBuffer[Y_MAIN_STICK]	=BB_ReadBuffer[5];
	
	reportBuffer[LEFT_TRIGGER]	=BB_ReadBuffer[6];
	
	reportBuffer[X_SECONDARY_STICK]=BB_ReadBuffer[7];
	reportBuffer[Y_SECONDARY_STICK]=BB_ReadBuffer[8];
	
	reportBuffer[RIGHT_TRIGGER]	=BB_ReadBuffer[9];
	
	if (Controller.Type != 121 ) //we do not want the DS2 dal and slider button support for bridge, it really messes up sbes mouse for one
	{
		reportBuffer[SLIDER]=BB_ReadBuffer[10];	
		reportBuffer[DIAL]=BB_ReadBuffer[11];
	}

	//TODO  -  need testing
	//says if the contrller is not a psx (not pressure) then fill in the press with analog triuggers
	//but looks like this is for  only.. so maybe put that in the ps2 code...
	if ( !Controller.Pressure ) //if not pressure data, check L and R 
	{
		pressureData[l2__pressure]=(BB_ReadBuffer[6]);
		pressureData[r2__pressure]=(BB_ReadBuffer[9]);
	}
	
	reportBuffer[HAT] = BB_ReadBuffer[12] ;		

	checkControllerType( ) ;
	if (modeTimer > 50) { modeTimer =0; getInfo();}//once every so often or if triggered from the above. 
	modeTimer++;//Just loop over, can set a limit if needed

}


////////////////////////////////
// Intended to be ongoing no  //
// exit and no retry. Bridge  //
// is a once in done deal	  //
////////////////////////////////	
int main(void) 
{		
	unsigned char con=false;		

	Controller.AutoPaused=false;	
	_autoPausePressed = false;
	oldRumbleState =0;
	modeTimer = 0;
	doMap = 255;

	INIT_HARDWARE();// do hardware init. 
	con_init();//do the consol init.


	

	////////////////////////////////
	// current reducer			  //
	// it is not much but it does //
	// help. USB is the most 	  //
    // significant.		    	  //
	////////////////////////////////
	USB_Disable(); 				 //disable usb
	USBCON |= _BV(FRZCLK);  
	PLLCSR &= ~_BV(PLLE);  
	USBCON &= ~_BV(USBE);  
	 
	ADCSRA =    				 // disable ADC
	UCSR1B=UCSR1A=UCSR1C=		 //diable uart
	SPCR=0;		  				 //disable spi
	TWCR &= 0xFF ^ _BV(TWEN);	 // disable I2C	
		
	TCCR1A =TCCR1B=
	TCCR0A =TCCR0B=
	TCCR3A =TCCR3B=
	TCCR4A =0;//disable all timers. 
 
	//list of start up times 
	//: sega needed 1.3 sec
	_delay_ms(2000);  
	
	while ( !con ) {con = con_probe(); } //wait for console to answer. 

	//set up brdige
	bridgeInterface = getBridgeInterface();
	bridgeInterface->init();
	
	mapReset();  //clear mappings 
	con_clear(); //and data. 

		
	//initialize the BB and test for controller connected now that we found a console and there are no more long waits. 
	unsigned char init=255;
	bridgeInterface->write(BRIDGE_CONTROLLER_POLL);//must poll
	while ( init >= ERROR_START_VALUE)
	{
		init = do_LLAPI( ) ;
		//wait for BB to answer. 
	}

	////////////////
	//  main loop //
	////////////////

	while(1) 
	{
		//For any protocols, like I2C or spi, we will need to make sure they are syncrones or we need to wait for complete.
		main_update();
		//TODO idea, start a timer here with an int that exires after 16.6 ms, when it does, clear the data as it is old. This prevents that menu bug with sticky keys.
		con_update(); //remember, only spend 20 ms or so in here, otherwise data will not refresh. 

	}
}





















