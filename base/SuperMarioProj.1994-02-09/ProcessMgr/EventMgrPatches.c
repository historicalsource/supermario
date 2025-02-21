/*
	File:		EventMgrPatches.c

	Contains:	Routines which patch the Event Manager traps.

	Written by:	Erich Ringewald

	Copyright:	© 1986-1992 by Apple Computer, Inc., all rights reserved.

	Change History (most recent first):
	 
		<33>	 8/26/92	DTY		Change ModSquad’s funky conditional to use #ifdefs like everyone
									else.
		<32>	 8/26/92	DTY		Roll in latest changes for A/UX.
		<31>	 8/17/92	hjcr	Adding support for OCE's "defer drag" feature. Conditionally
									compiled under MODSQUAD
		<30>	 8/13/92	KST		#1039646 <PKE>: Stop passing activate events intended for
									floating system windows to applications. Fixes a crash in
									PhotoShop.
		<29>	 7/28/92	DCL		Included private Layers.h because applicationFloatKind &
									systemFloatKind constants were moved out of the public eye.
		<28>	 7/27/92	DTY		#1037295 <JH>: Change the last revision so that retVal is not
									assigned the result of GetNextPseudoEvent.  This allows update
									events to be processed if there are no pseudoevents.
		<27>	 7/20/92	KST		#1033230 <JH>: In Event_Common, if we get an update event but
									there is pending events in the PseudoEvent queue, then we should
									return that PseudoEvent instead of the UpdateEvent.
		<26>	 5/11/92	JH		#1029361 <KST>: Need to determine whether to send event to SWM
									driver via the WindowKind and not the refcon.  Changed
									event_common to check for WindowKind being systemFloatKind
									instead of looking at the refcon.
		<25>	 4/25/92	YK		#1027486: Move the code, which checks update regions for TSM
									windows, from nexttask() in Scheduler.c to event_common().
		<24>	  4/9/92	KST		#1025131,<JH>: Changed SWM from a background app to a driver so
									that input method can do dialog, change cursor, access the menu,
									and type into the dialog window.
		<23>	 3/23/92	JSM		OSEvents.h is obsolete, use Events.h.
		<22>	 1/11/92	YK		One more change to checking.
		<21>	 1/11/92	YK		reflect new scheme to check to see if the keydown event should
									be sent to the floating window app.
		<20>	 1/10/92	YK		Check if the current app has a layer before calling
									CheckUpdateIn. This is done for the Service Window Manager app
									(for TSM) which does not have an application layer but has a
									floating layer.
		<19>	11/17/91	YK		Added code for the Text Services Manager to support apps which
									set SEvtEnb flag off. WakeUp the front process when text is
									posted by the TSM.
		<18>	10/16/91	YK		Added code for InlineAware app.
		<17>	10/15/91	YK		Added code for posting and getting a fake-keyDown event
									generated by the Text Service Manager.
		<16>	 10/9/91	YK		Changed event_common so that it ignores events that belong to a
									floating window that a text service has put up.
		<15>	 3/20/91	DFH		gbm,#84989: event_common calls MakeSynStrikesPurgeable.
		<14>	 2/18/91	DFH		bbm,BRC#82666:Fix PRUPDATE state of event_common to respect
									context switch disabling.
		<13>	 1/21/91	DFH		(KSM) Process Menu is now called Application Menu.
		<12>	 1/14/91	DFH		(JDR) Conditionalize out AUX support.
		<10>	  1/8/91	DFH		(rdd) Changed event_common to allow major switch even if front
									process is suspended for high-level debugging.
		 <9>	12/20/90	DFH		(dba) Convert GetNextHighLevelEvent to a dispatch.
		 <8>	12/13/90	DFH		(JWM) Fixed PRRUN check of p_wakeuptime that bypasses
									cpu_resched. Was checking for 0, rather than <= TICKS, which
									falsely skipped cpu_resched so we ran nothing but the front app.
		 <7>	 12/5/90	DFH		Integrate AUX support.
		 <6>	11/27/90	DFH		Removed unneeded parameter from cpu_resched.
		 <5>	11/19/90	DFH		Change trespass() to dbmsg() in event_common default case for
									p_state.
		 <4>	11/15/90	DFH		Fixed race condition in WaitNextEvent that can lead to long
									sleep when thrashing under VM.
		 <3>	 11/6/90	JWM		In event_common we flush the outstanding HLEvent by calling
									flushMsg. The second parameter can be one of three values:
									msgWasFullyAccepted, msgWasParitallyAccepted and
									msgWasNotAccepted. This parameter is determined by using
									msgStatus. In previous versions flushMsg was being called with
									one of the two possiblities instead of one of three
									possiblities.
		 <2>	10/30/90	csd		(really Jay Moreland) Fixed bug where a partially accepted
									message causes us to return message not accepted at all.
		 <0>	 x/xx/86	ELR		New Today.

*/  
 
#include <types.h>
#include <memory.h>
#include <toolutils.h>
#include <osutils.h>
#include <quickdraw.h>
#include <fonts.h>
#include <events.h>
#include <desk.h>
#include <resources.h>
#include <retrace.h>
#include <menus.h>
#include <windows.h>
#include <files.h>
#include <segload.h>
#include <errors.h>
#include <eppc.h>
#include <MFPrivate.h>
#include <Layers.h>


#include "Glue.h"
#include "Lomem.h"
#include "Data.h"
#include "SysMisc.h"
#include "Patches.h"
#include "OSDispatchPrivate.h"
#include "Aux.h"
#include "EppcPrivate.h"
#include "Puppet.h"
#include "Hlist.h"
#include "Zone.h"

/* Global items used by the event manager patches and extensions. */
RgnHandle	mousebox;
Point		lastmouse;

/* Some function prototypes that should be in (yet another) header file */
short 		GetNextPuppetEvent(u_short, EventRecord *, Ptr, Boolean);
Boolean		PuppetForceButtonDown(void);
void		AdjustApplicationMenu(PEntryPtr);
void		pollmouse(void);
PEntryPtr	fg_resched(EventRecord *, Boolean);
short		Coercion_State_Engine(unsigned short, EventRecord *, Ptr, Boolean);
void		CancelSleep(PEntryPtr);
void		CancelPendingUpdates(LayerPtr);
void		LimitFrontSleepForDriversSake(void);
void		LimitBackSleepForDriversSake(void);
void		MakeSynStrikesPurgeable(void);

/* Function prototypes internal to this file */
short		event_common(unsigned short, EventRecord *, Boolean);
short		GetNextDeskUpdateEvent(unsigned short, EventRecord *);
short		GetNextPseudoEvent(short, EventRecord *, Boolean);
void		PostPseudoEvent(PEntryPtr, EventRecord *);
short		GetFakeKeyEvent(EventRecord *, Boolean);
Boolean		imLayerKeyDown(EventRecord *, Boolean);
Boolean		OSEventAvailForFront(short, EventRecord *);

/************************************************************************************
 *						Toolbox event calls according to us.
 ************************************************************************************/
	
/* c_waitnextevent. Our implementation of the WaitNextEvent trap.  Handles events like
 * original GetNextEvent does, but allows caller to optimize CPU usage for a multiple
 * application environment.  The "timeout" parameter specifies the amount of time we can
 * idle the calling process (p_state == PRSLEEPING) while no events are available.  We
 * resume (wake up) the process when a) the time expires, b) an event becomes available,
 * or c) the process is woken up by someone calling WakeupProcess against it.  The
 * "mouseRgn" parameter specifies the screen area where the caller does not care to check
 * the mouse position (many apps want to do things like change cursor shapes based on
 * mouse position, so they need event time to do it).  We wake up the process and give it
 * a "mouse moved" event (PSEVT_MOUSEMOVED) when the mouse strays from the region.  This
 * allows processes to sleep, yet maintain nice cursor shapes.
 */
pascal short
c_waitnextevent(unsigned short eventmask, EventRecord *theevent, unsigned long sleep, RgnHandle mouseRgn)
	{
	register unsigned long	wakeUpTime, currTime;
	unsigned long			olda5;
	short					retval;
#define ALWAYS_INVALID_PT		(-1)
	
	olda5 = ProcessMgrA5Setup();
	pCurrentProcess->p_waitnexteventlevel++;
	
	/* Invalidate the mouse position, so that we check the first time thru pollmouse(). */
	if (pCurrentProcess == pFrontProcess) 
		{
		mousebox = mouseRgn;
		LONG_TO_PT(ALWAYS_INVALID_PT, &lastmouse);
		}

	/* Calculate time when we can roust, allowing for overflow. */
	wakeUpTime = TICKS + sleep;
	if (wakeUpTime < sleep)
		wakeUpTime = MAXVALUE;
	pCurrentProcess->p_wakeuptime = wakeUpTime;
	
	/* Look for event, sleep until prescribed wakeup time if there are none.  Put in a
	 * loop so that we re-look right away if the app is woken by someone.
	 */
	do
		{
		/* Look for event from event machine. */
		if ((retval = GetNextEvent(eventmask, theevent)) != wFalse)
			break;
		
		/* Get out if we're at or past the wakeup time (p_wakeuptime might have changed)
		 * NOTE: We stash the p_wakeuptime and TICKS in local variables to be atomic
		 * between their comparison and the call to SleepProcess.  During that period, a
		 * change in TICKS, or a zero-ing of p_wakeuptime at interrupt level would give
		 * incorrect results (a very high sleep time).  Most likely with VM.
		 */
		if ((currTime = (unsigned long) TICKS) >= (wakeUpTime = pCurrentProcess->p_wakeuptime))
			break;

#ifdef HAS_AUX_PROCESSMGR
		/* If the current process has been given a wakeup via WakeupProcess, we take
		 * note here, and break out before going to sleep.
		 * NOTE: This is critical to the performance of MacX 1.1.7
		 * NOTE: The wakeup_flag entry in PEntry is only set when A/UX is running
		 */
		if (AUXIsPresent)
			if (pCurrentProcess->wakeup_flag)
				{
				pCurrentProcess->wakeup_flag = 0;
				break;
				}
#endif HAS_AUX_PROCESSMGR
	
		/* Base sleep period on desired wakeup time.*/
		SleepProcess(wakeUpTime - currTime);
		
#ifdef MODSQUAD
		/* 	check if we have a request to start a drag in the background	*/
		if (gDragState == DRAG_INIT)
			{
			*theevent = gDragEvent;
			theevent->what = bgMouseDown;
			//theevent->message = gStartDragTime;
			//theevent->where = gStartDragPosition;
			retval = wTrue;
			if (pCurrentProcess->p_state != PRBACKRUN)
				Debugger();
			pCurrentProcess->p_state = PRSTARTDRAG;
			break;
			}
#endif
		/* Loop for further GetNextEvent if app was woken early. */
		} while (pCurrentProcess->p_wakeuptime == 0);
		
	pCurrentProcess->p_waitnexteventlevel--;
	pCurrentProcess->p_wakeuptime = 0;
	A5Restore(olda5);

	/* Toolbox may look at low byte of word */
	return (retval ? wTrue : wFalse);
	}

/* c_getnextevent.  Our patch to the toolbox _GetNextEvent.  Deals with the events we
 * make, fake, or hide.  Different than WaitNextEvent, in that caller can not optimize
 * CPU usage for multiple applications.  We wish this trap didn't exist, and that
 * everyone called WaitNextEvent with nice optimization parameters.
 */
pascal short
c_getnextevent(unsigned short eventmask, EventRecord *theevent)
	{
	return(event_common(eventmask, theevent, true));
	}
	
/* c_eventavail.  Patch for EventAvail.  Same comments as c_getnextevent. */
pascal short
c_eventavail(unsigned short eventmask, EventRecord *theevent)
	{
	return(event_common(eventmask, theevent, false));
	}

/* event_common.  This routine replaces both GetNextEvent and EventAvail, and
 * the Boolean pullevent determines which is which. Many of the different
 * process states and scrap coercion states are parsed here and the
 * different event paths are created.
 */
short
event_common(unsigned short eventmask, EventRecord *theevent, Boolean pullevent)
	{
	register short	retval;
	PEntryPtr		suggProc;
	MFmsgBlkPtr		pMsgBlk;
	pascal void		(*oldtrap)();
	unsigned long	olda5;
	Boolean			gotFakeKeyDown;
	HListHdl		pseudoEvtQHListHdl;
	
	
	retval = wFalse;
	olda5 = ProcessMgrA5Setup();
	assert((pCurrentProcess != pNullProcess) && (pCurrentProcess->p_state != PRNULL));
	oldtrap = patchtraps[pullevent ? GETNEXTEVENT : EVENTAVAIL].oldtrap;
	pCurrentProcess->p_eventmask = eventmask;
	gotFakeKeyDown = false;

	/* Loosen Font Mgr's grip on some system heap memory, since Font Mgr has no way to
	 * do it.
	 */
	MakeSynStrikesPurgeable();
	
#ifdef MM_DEBUG
	/* Informative check for enough slop in system heap */
	if (HEAP_FREE_SPACE(SYSZONE) < 2*1024)
		dbmsg("Sys heap dangerously low...");

	/* Check other stats */
	CheckSysMapForBadMPs();
	CheckZoneForBadFreeMPs(SYSZONE);
	CheckZoneForBadFreeMPs(ProcessMgrZone);
#endif MM_DEBUG
	
	/* Use it or lose it!  Remove EPPC message that was prepared in previous event call,
	 * but has not been accepted by the application.
	 * NOTE: This behavior means we have to be SURE to return an EPPC event found in
	 * GetNextHighLevelEvent.  Deciding to postpone (i.e. turn to a nullEvt) such an
	 * event virtually guarantees the message will be lost.
	 */
	if (pullevent && ((pMsgBlk = pCurrentProcess->eppcBlk.mfMsgBlk) != nil))
		{
		if (pMsgBlk->msgStatus & (msgCompletelyAccepted+msgPartialyAccepted))
			flushMsg(pMsgBlk,(pMsgBlk->msgStatus & msgCompletelyAccepted) ? msgWasFullyAccepted:msgWasPartiallyAccepted);
		else
			flushMsg(pMsgBlk,msgWasNotAccepted);
		pCurrentProcess->eppcBlk.mfMsgBlk = nil;
		}

	/* Fill in where, when, etc. */
#define noEventMask 0
	(void)GetOSEvent(noEventMask, theevent);
	
	/* How we look for events depends on the process state */
	switch (pCurrentProcess->p_state) {
	
		/* Process is running in the front layer. */
		case PRRUN:
label_PRRUN:
			{
			/* NOTE:  Conditional a hack to make FullPaint work since it calls
			 * _GNE again after getting a mouse-down in the menu bar, but we
			 * currently have no timeout on waiting for _MenuSelect.  However,
			 * we do know that it calls _GNE looking for updates only.  This is
			 * really gross, but I feel the need to fix them since they worked
			 * (by luck) in MF 1.0 and stopped because of breaking the line below out of
			 * the puppet stuff (to make cancel work correctly).
			 * NOTE:  This should be done on transition from PRPUPPET to PRRUN, but we
			 * can't do it until _MenuSelect has had it's crack at it.
			 */
			if (eventmask != updateMask)
				forcedPupStrMenuSelection = 0L;
			
			switch (coercionState)
				{
				case CS_DONE:
					{
					/* Give time to DAs.  We have to recheck coercionState, since it could
					 * have changed in a DA call to GNE or WNE.
					 */
					if ((pCurrentProcess->p_waitnexteventlevel != 0) && (dont_switch == 0))
						{
						SystemTask();
						if (coercionState != CS_DONE)
							{
							assert(pCurrentProcess->p_state == PRRUN);
							goto label_PRRUN;
							}
						}

					/* Look for fake-keyDown events generated by the Text Services Manager. */
					if ( (fakeKeyCount > 0) && (eventmask & keyDownMask) && ((retval = GetFakeKeyEvent(theevent, pullevent)) != wFalse) )
						{
						gotFakeKeyDown = true;
						if ( pullevent )
							{
							/* Give this to DA, if any.  But don't give it to TSM so that we set dontGetFakeKey.
							 * dontGetFakeKey will be reset in c_systemevent and be refered by imLayerKeyDown.
							 * if SEVTENB is off, we rely on app's good behavior. they should call SystemEvent.
							 */
							dontGetFakeKey++;					// set the flag so that we won't send it to SWM
							if ( (SEVTENB != 0) && SystemEvent( theevent) )
								retval = wFalse;
							}
						}

					if ( IMLayer!=nil )
						{
						EventRecord	updateEventForSWM;
						LayerPtr	saveLayer;
						if ( CheckUpdateIn( &updateEventForSWM, IMLayer) )
							{
							if ( ((WindowPeek) updateEventForSWM.message)->windowKind == systemFloatKind)			//	see is the reference is the kind for a system owned floater
								{																					//	this layer belongs to the SWM driver
								saveLayer = SwapCurLayer(IMLayer);
								SendEventToSWMDriver( &updateEventForSWM );								//	so route the event to driver
								SetCurLayer( saveLayer );
								}
							}
							//	we may still have an update rgn for another window, take care of it next time.
						}

					/* Look for events.  Assume pseudoevent delivery doesn't require a
					 * foreground switch.
					 * NOTE: We rely on our patch to SystemEvent to keep us from seeing
					 * an update event for another process' layer.
					 * NOTE for the Text Services Manager: We also rely on SystemEvent to trap
					 * all keyDown events that should be sent to the Text Service Module.
					 * But EventAvail does not call SystemEvent, so we have to take care of this case.
					 */
					if ( !gotFakeKeyDown )
						{
						
						if ((retval = CALL_FNC_PTR(Eventcommon_ptr, oldtrap,(eventmask, theevent))) == wFalse) {	/* if we don't have an event */
							if ((retval = GetNextPseudoEvent(eventmask, theevent, pullevent)) != wFalse)			/* then check PseudoEvent */
								break;
						}
						else {																						/* if we DO have an event, and		<#27> */
						if ((theevent->what == updateEvt 		&&													/* if we got an update event, and	<#27> */
							(HListExists(pseudoEvtQHListHdl = (*(pCurrentProcess->p_pcb))->p_PseudoEvtQHList) && 	/* we have events queued			<#27> */
							(GetFirstHListElem(pseudoEvtQHListHdl) != nil) )))										/*			....					<#27> */
						
						// <28> From the last change, retVal was assigned the boolean result of the next GetNextPseudoEvent.
						//		However, if GetNextPsuedoEvent returns false, the Process Manager changes the event from
						//		an update event to a null event, causing the update event to never be processed, and thus
						//		causing modal dialogs to come up blank.
						//		If GetNextPseudoEvent returns false, return the original update event.  Because we know we’ll
						//		have an update event at this point, retVal will always be true, so we don’t need to assign
						//		the result of GetNextPseudoEvent to it.  (If GetNextPseudoEvent returns true, retVal is already
						//		true.  If GetNextPseudoEvent returns false, retVal should still remain true so that the update
						//		event will get passed back to the caller.)
						
								if (GetNextPseudoEvent(eventmask, theevent, pullevent) != wFalse)					/* go get the first queued event	<#27> */
									break;																			/* and return that (don't worry about update we'll get it the next time through */
						}																							/* perfect, I hope					<#27> */

						/* If an event arrived from ToolBox Event Manager, and this is an EventAvail call or
						 * SEVTENB is currently off that mean SystemEvent was not called, then check to see if
						 * this event should be sent to the Text Service Module.
						 */
						if ( (retval != wFalse) && (!pullevent || (SEVTENB == 0)) && imLayerKeyDown(theevent, false) )
							{
							retval = wFalse;
							theevent->what = nullEvent;
							}
						}

#ifdef HAS_AUX_PROCESSMGR
					/* check A/UX specific events */
					if (AUXIsPresent)
						(void) AUX_HandleEvent(theevent);
#endif HAS_AUX_PROCESSMGR
					
					/* Cut out early if process switching temporarily disabled */
					if (dont_switch != 0)
						break;
					
#ifdef MODSQUAD
					gDragState = DRAG_DONE;
#endif
					/* Check whether we should begin a foreground (major) switch */
					if ((suggProc = fg_resched(theevent, pullevent)) != pCurrentProcess)
						StartForegroundSwitch(suggProc);
					
					/* Do generalized scheduling if this FG process is beyond quantum end, and is
					 * not planning on sleeping (SleepProcess will handle rescheduling).  We take
					 * this opportunity to reduce the front process' sleep time to match the
					 * "needs time" requirements of whatever drivers are in the unit table.
					 */
					else if ((retval == wFalse) || (theevent->what == nullEvent) )
						{
						LimitFrontSleepForDriversSake();
						if ((pCurrentProcess->p_wakeuptime <= TICKS) && (TICKS > nextswitch))
							cpu_resched();
						}
					
					/* phew! */
					break;
					}
				
				/* Any state other than CS_DONE means we have coercing going on...
				 * Look for an event from the scrap coercion state machine.  Make sure that
				 * app does not sleep just because we are force feeding it a null event.
				 */
				default:
					{
					retval = Coercion_State_Engine(eventmask, theevent, oldtrap, pullevent);
					CancelSleep(pCurrentProcess);
					break;
					}
				}
			break;
			}

/* MicroSoft word processors have a problem with background updates.  To help
 * out, we give them a few null events after the first update event (even
 * if that is the only update we give it).
 */
#define IS_OLD_MICROSOFT_WORD_PROCESSOR(pProc) \
	(	((pProc)->p_signature == 'MSWD' && (pProc)->p_version < '0400') \
	||	(pProc)->p_signature == 'WORD' \
	||	(pProc)->p_signature == 'MSWT' )
#define REQUIRED_NULLS_FOR_OLD_MICROSOFT_WORD (2)							
		
		/* Process is running, but not in the front layer, and is only allowed to
		 * see update events (this lets background windows get timely updates without
		 * sucking too much other time).
		 */
		case PRUPDATE:
			{
			/* Prevent app from sleeping since there may be other events pending */
			CancelSleep(pCurrentProcess);
			
			/* Ran out of chances?  If so, get rid of update regions and continue.
			 * NOTE: If we can't switch, we set the p_updatechances to 1 without changing
			 * p_state.  This puts the current process in a holding pattern until
			 * switching is enabled.
			 */
			if (remnulls == 0 && --pCurrentProcess->p_updatechances == 0)
				{
				CancelPendingUpdates(pCurrentProcess->p_layer);
				if (dont_switch == 0)
					cpu_resched();
				else
					pCurrentProcess->p_updatechances = 1;
				break;
				}
			
			/* Should we give a null event?  Break herein results in null event. */
			if (remnulls > 0)
				{
				/* always give null event to EventAvail */
				if (pullevent == false)
					break;
				
				/* hand out one more null to GetNextEvent */
				remnulls -= 1;
				break;
				}
			
			/* Look for updates to handle.  If we have no more left, CheckUpdate will
			 * return false, and we should immediately offer up the CPU since that is
			 * the definition of the PRUPDATE state.
			 * NOTE: We would prefer cpu_resched to return the process to its previous
			 * state (i.e. the one it was in when PRUPDATE was forced), rather than
			 * PRRREADY.  When this guy comes back out of cpu_resched, he should probably
			 * go to the top of this routine again.
			 */

			/* Look in desklayer, if current app is managing the desktop */
			if ( (retval = GetNextDeskUpdateEvent(eventmask, theevent)) != wFalse )
				break;

			/* Check this application's layer.
			 * NOTE: If we can't switch, we set the p_updatechances to 1 without changing
			 * p_state.  This puts the current process in a holding pattern until
			 * switching is enabled.
			 */
			if ( !(  ((pCurrentProcess->p_layer != nil)										/* <20> */
						&&(CheckUpdateIn(theevent, pCurrentProcess->p_layer) != false) )
				   ||((pCurrentProcess->p_floatLayer != nil)
						&&(CheckUpdateIn(theevent, pCurrentProcess->p_floatLayer) != false) ) )
				)
				{
				if (dont_switch == 0)
					cpu_resched();
				else
					pCurrentProcess->p_updatechances = 1;
				break;
				}

			/* Update is for this application.  Give it only if app isn't masking them
			 * and the system doesn't want it (it shouldn't, we know it is for this app).
			 */
			if ((eventmask & updateMask) != 0)
				{
				/* Don't call SystemEvent if this was EventAvail, or if SEVTENB is off. */
				if ( (pullevent == false) || (SEVTENB == 0) || (SystemEvent(theevent) == false) )
					retval = wTrue;
		
				/* Cater to old versions of MicroSoft Word by throwing in some
				 * gratuitous null events before giving background updates
				 * NOTE: Why isn't remnulls initialized when p_state is set
				 * to PRUPDATE?  We've already checked remnulls at the beginning
				 * of this routine. Doesn't this mean we looked at an uninitialized
				 * value?
				 */
				if (retval)
					{
					PEntryPtr	pProc = pCurrentProcess;
	
					if (IS_OLD_MICROSOFT_WORD_PROCESSOR(pProc) && pullevent)
						remnulls = REQUIRED_NULLS_FOR_OLD_MICROSOFT_WORD;
					}
				}
				
			/* get out of dodge city, pardner */
			break;
			}
		
		/* Process is being brought to the front.
		 * NOTE: Actually want state PRFIRSTMOVING below the if compound statement
		 * and don't check for slices here.
		 */
		case PRMOVING:
			{
			LayerPtr		pCurLayer;
			
			/* Draw the menu bar (special case to initialize the menus if the app is new.
			 * Application menu is adjusted as if app is already frontmost.
			 */
			if ((pCurLayer = GetLayer()) != nil)
				{
				if (pCurrentProcess->p_slices > 1)
					{
					if (MENULIST == nil)
						InitMenus();
					else
						{
						AdjustApplicationMenu(pCurrentProcess);
						DrawMenuBar();
						}
						
					SYSEVTMASK = (*pCurrentProcess->p_pcb)->sysevtmask;
					}
				
				/* Red rover, red rover, let the windows come forward! */
				MakeTopLayer(pCurLayer);
				}
			
			/* Go to PRRUN so that we don't return any extraneous events if we are
			 * re-entering from the debugger.  If we return even a single null event,
			 * we might induce the app to unload all its code segments, which might
			 * cause the debugger to be called again, which will start the whole
			 * vicious cycle over again.  We *must* guarantee that the event state
			 * machine plows forward every time, in all cases.
			 */
			pCurrentProcess->p_state = PRRUN;
			goto label_PRRUN;
			}
			
		/* Process is running in mode where only artificial events are allowed */
		case PRPUPPET:
			{
			retval = GetNextPuppetEvent(eventmask, theevent, oldtrap, pullevent);
			break;
			}
		
#ifdef MODSQUAD
		case PRSTARTDRAG:	/* Last event was bgMouseDown	*/
			pCurrentProcess->p_state = PRREADY;
			if (gDragState != DRAG_DONE)
				gDragState = DRAG_IGNORED;
			switch_task(gFrontDragProcess);
			/* In case our state changed while we were switched out */
			if (pCurrentProcess->p_state != PRBACKRUN)
				break;
			/* continue with PRBACKRUN */
#endif
		/* Process is running, but not in the front layer */
		case PRBACKRUN:
			{
			PEntryPtr	pProc;
			
			/* Special case for when the first slice a process gets is in the background.
			 * Give it a suspend event so process knows it's in back.  p_isfirstbackevt
			 * is initialized true only if the app has both modeNeedSuspendResume and
			 * modeDoesActivateOnFGSwitch, so others are naturally skipped here.
			 */
			if ( (pCurrentProcess->p_isfirstbackevt) && (pCurrentProcess->p_slices == 1) )
				{
				if ((eventmask & app4Mask) != 0)
					{
					theevent->what = app4Evt;
					theevent->message = SUSPEND_MESSAGE;
					retval = wTrue;
					}
				pCurrentProcess->p_isfirstbackevt = false;
				break;
				}
			
			/* Start pending foreground switch if the current front process is unable to. 
			 * NOTE: We will be switched out by StartForegroundSwitch.  This should be
			 * OK, and we want it to continue the normal event call when it is
			 * scheduled again.
			 */
			if ( (pFrontProcess->p_state == PRNULL) && 
				((pProc = PopFrontProcessQueue()) != nil) &&
				(pProc != pFrontProcess) )
				{
				CancelSleep(pProc);
				StartForegroundSwitch(pProc);
				break;
				}

			/* Give time to (local) DA's even if process is unable to */
			if ( (pCurrentProcess->p_waitnexteventlevel != 0) && (dont_switch == 0) )
				SystemTask();
			
			/* Offer up the CPU if sleepers are waking, or there is an OS event (cuz
			 * it's probably not for a BG app like us), or our "quantum" has expired.
			 * Note that, unlike in PRRUN state, we let the quantum expire even if
			 * there are still events pending for the process.
			 */
			if ( (dont_switch == 0) &&
				((TICKS >= nextswitch) || napOver ||
					OSEventAvailForFront(pFrontProcess->p_eventmask, theevent)
#ifdef HAS_AUX_PROCESSMGR
					|| ((AUXIsPresent == false) || AUX_KernelEvents(theevent))
#endif HAS_AUX_PROCESSMGR
					) )
				{
				cpu_resched();

#ifdef MODSQUAD
				/* 	check if we have a request to start a drag in the background	*/
				if (gDragState == DRAG_INIT)
					{
					*theevent = gDragEvent;
					theevent->what = bgMouseDown;
					//theevent->message = gStartDragTime;
					//theevent->where = gStartDragPosition;
					retval = wTrue;
					if (pCurrentProcess->p_state != PRBACKRUN)
						Debugger();
					pCurrentProcess->p_state = PRSTARTDRAG;
					break;
					}
#endif
				/* In case our state changed while we were switched out */
				if (pCurrentProcess->p_state != PRBACKRUN)
					break;
				}
				
			
			/* Look for events in the order: high-level, desk updates, pseudo */
			if (GetNextHighLevelEvent(eventmask, theevent, pullevent) ||
				GetNextDeskUpdateEvent(eventmask, theevent) ||
				GetNextPseudoEvent(eventmask, theevent, pullevent))
				retval = wTrue;
			else
				LimitBackSleepForDriversSake();
					
			break;
			}
			
		default:
			{
			dbmsg("Toolbox event manager trap was called when the current application was not running.");
			break;
			}
		}
	
	/* Call the debugger routine that wants to know the app is about to regain control. */
	if (pDebugProcess != nil && debugToAppRoutine != nil)
		CALL_FNC_PTR(DebugToAppRoutine_ptr, debugToAppRoutine, ());
		
	A5Restore(olda5);
	
	/* Enforce correspondence between Boolean return value and event type.  Allows
	 * above routines to return non-false, to break PC out of the switch statement, but
	 * still return a null event (i.e. precedence is given to retval, not the event).
	 * Also, guarantees that the function result is either wTrue or wFalse.
	 */
	if (retval == wFalse)
		theevent->what = nullEvent;
	else 
		{
		retval = wTrue;
		if (theevent->what == nullEvent)
			retval = wFalse;
		if (theevent->what == activateEvt) {		// we should never send the event to apps when the window is a floating window
				//
				// after all an activate event isn't really relevant to a floating window
				//
				if ( ((WindowPeek) theevent->message)->windowKind == systemFloatKind) {
					theevent->what = nullEvent;
					retval = wFalse;
				}
			}
		}

	return(retval);
	}

/************************************************************************************
 *								 Utility routines.
 ************************************************************************************/

/* GetNextDeskUpdateEvent.  Checks for desk update events if current application
 * manages the desk layer.  Normally, desk updates are delivered via CheckUpdateIn
 * of the application layer (since the owner has copied the updateRgn handle into
 * a new window record in that layer).  If, however, the application layer is hidden…
 * this delivery can not occur.  We make this explicit check.  It doesn't hurt to do
 * it even when the application layer is not hidden.
 * Assumes A5 == PROCESSMGRGLOBALS
 */
short
GetNextDeskUpdateEvent(unsigned short eventmask, register EventRecord *theevent)
	{
	short	retval;

	retval = wFalse;
	if ( (desklayerowner == pCurrentProcess) &&
		((eventmask & updateMask) != 0) &&
		(EmptyRgn(((WindowPeek) desklayer)->updateRgn) == false) )
		{
		theevent->what = updateEvt;
		theevent->message = desklayer;
		theevent->when = TICKS;
		PT_TO_PT(&MOUSE,&theevent->where);
		retval = wTrue;
		}
		
	return(retval);
	}

/* pollmouse.  Check mouse activity. This routine will determine whether or not to
 * send a mouse moved event to the frontmost application.  Only do it for tasks
 * calling _WNE (because mouse-moved is not defined for EventAvail).
 */
void
pollmouse(void)
	{
	EventRecord		mouse_event;

	if (MOUSE == lastmouse)
		return;
		
	lastmouse = MOUSE;
	if ( (mousebox != nil)
		&&	(pFrontProcess->p_waitnexteventlevel != 0)
		&&	(PtInRgn(lastmouse, mousebox) == false) )
		{
		mouse_event.what = PSEVT_WHAT;
		*((unsigned char *) &mouse_event.message) = PSEVT_MOUSEMOVED;
		mouse_event.where = lastmouse;
		mousebox = nil;
		PostPseudoEvent(pFrontProcess, &mouse_event);
		}
	}

/* GetNextPseudoEvent.  Retrieve the first pending pseudo-event, if any.
 * A5 World needed: PROCESSMGRGLOBALS
 */
short
GetNextPseudoEvent(short eventmask, EventRecord *pReturnEvent, Boolean pullevent)
	{
	HListHdl			pseudoEvtQHListHdl;
	EventRecord			**eventHandle;
	short				retval = wFalse;
	
	/* Get the (unlocked!) handle to the queue and look for an element. */
	if (HListExists(pseudoEvtQHListHdl = (*(pCurrentProcess->p_pcb))->p_PseudoEvtQHList) && 
		((eventHandle = (EventRecord **) GetFirstHListElem(pseudoEvtQHListHdl)) != nil) )
			{
			register EventRecord		*pQdEvent = *eventHandle;
			
			if ((eventmask & (1 << pQdEvent->what)) != 0)
				{			
				*pReturnEvent = *pQdEvent;
				if (pullevent)
					{
					RemoveFromHList(pseudoEvtQHListHdl, (HListElem)eventHandle);
					DisposHandle(eventHandle);
					}
				retval = wTrue;
				}
			}

	return(retval);
	}

/* PostPseudoEvent.  Posts copy of the given pseudo-event to the specified process.
 * NOTE: Assumes a5 == PROCESSMGRGLOBALS
 */
void
PostPseudoEvent(PEntryPtr pProc, EventRecord *pEvent)
	{
	PCB				**pcbHdl;
	HListHdl		pseudoEvtQHListHdl;
	EventRecord		**ppEventRecord;
	THz				saveTheZone;

	/* Switch to the ProcessMgrZone */
	SafeSetZone(saveTheZone, ProcessMgrZone);
	
	/* Get the handle to the queue */
	pcbHdl = pProc->p_pcb;
	pseudoEvtQHListHdl = (*pcbHdl)->p_PseudoEvtQHList;
	if (HListExists(pseudoEvtQHListHdl)
		|| (pseudoEvtQHListHdl = CreateHList(1), (*pcbHdl)->p_PseudoEvtQHList = pseudoEvtQHListHdl) != nil)
			{
			if ((ppEventRecord = (EventRecord **) NewHandleClear(sizeof(EventRecord))) != nil)
				{
				/* Set the time */
				pEvent->when = TICKS;
				
				/* Copy the event record to the allocated space */
				**ppEventRecord = *pEvent;
				
				/* And then queue it up */
				AddToHList(pseudoEvtQHListHdl, (HListElem)ppEventRecord);
				
				/* Wake him up */
				CancelSleep(pProc);
				}
			}
		
	/* Restore the old zone, if it wasn't the MF heap */
	SafeRestoreZone(saveTheZone);
	}

/************************************************************************************
 *						Text Services Manager supporting routines.
 ************************************************************************************/

Boolean					// return true if we send the event to SWM
imLayerKeyDown(EventRecord *theEvent, Boolean pullEvent)
	{
	EventRecord			anEvent;
	short				eventWhat;

	if ( ( (theEvent->what == keyDown) || (theEvent->what == autoKey) )
		&& (tsmLayerOwner != nil) && (dontGetFakeKey == 0)
		&& (routeEvent!=false) && (pFrontProcess->p_inlineAware==false) )
		{
		//PostPseudoEvent( tsmLayerOwner, theEvent);
		//WakeUpProcess(&tsmLayerOwner->p_serialNumber);
		/***
			This event needs to go through the SWM driver for possible input method processing.
			Set up the control block and let it fly.
		***/
		
		// Oh, you'll love this. When we send this event to SWM, SWM will send it to TSM, TSM will send it to IM.
		// If IM handled this keydown event, it will change this event to NULL event. The code that remove the event
		// from the OS event queue was relying on 1 << (theEvent->what) which is NULL now. The event will not get
		// removed from the queue and IM will see the same event twice !!
		
		// And, if we remove the event from the queue after SendEventToSWMDriver, it will work fine 99% of the cases.
		// However, there will be an amoly where this will crash the system.
		
		eventWhat = theEvent->what;
		
				
		if ( !pullEvent )
			GetOSEvent( 1 << eventWhat, &anEvent);					// use eventWhat
		
		SendEventToSWMDriver( theEvent );							// so route the event to driver

		
		return true;
		}
	else
		return false;
	}
	
/* GetFakeKeyEvent.  Retrieve the first Fake-KeyDown event generated by the Text
 * Service Manager.  Assumes that the caller wants key-down event.
 */
short
GetFakeKeyEvent(EventRecord *theEvent, Boolean pullEvent)
	{
	FakeKeyPtr		pFakeKey;
	short			newSize;
	THz				saveTheZone;

	if (fakeKeyCount>0)			/* Make sure we have it */
		{
		pFakeKey = *fakeKeyHandle;
		/* Assumes that 'when' & 'where' fields have been filled in. */
		theEvent->what		= keyDown;
		theEvent->message	= pFakeKey->message;
		theEvent->modifiers	= pFakeKey->modifiers;

		if ( pullEvent )
			{
			SafeSetZone(saveTheZone, ProcessMgrZone);

			newSize = sizeof(FakeKeyRecord)*(--fakeKeyCount);
			BlockMove( pFakeKey+1, pFakeKey, newSize);
			SetHandleSize( (Handle)fakeKeyHandle, newSize);

			SafeRestoreZone(saveTheZone);
			}

		return wTrue;
		}
	else 
		return wFalse;
	}
	
	
	
	
pascal OSErr
C_PostFakeKeyDown(EventRecord *theEvent)
	{
	FakeKeyPtr		pFakeKey;
	unsigned long	olda5;
	OSErr			err;
	THz				saveTheZone;

	if ( (theEvent->what != keyDown) && (theEvent->what != autoKey) )
		return paramErr;

	olda5 = ProcessMgrA5SimpleSetup();
	SafeSetZone(saveTheZone, ProcessMgrZone);

	/* Allocate memory for this event */
	SetHandleSize( (Handle)fakeKeyHandle, sizeof(FakeKeyRecord)*(fakeKeyCount+1));
	if ( (err = MEMERROR) == noErr )
		{
		/* Point to the new element then increment counter. */
		pFakeKey = *fakeKeyHandle + (fakeKeyCount++);
		pFakeKey->message	= theEvent->message;
		pFakeKey->modifiers	= theEvent->modifiers;
		}
	WakeUpProcess(&pFrontProcess->p_serialNumber);
	SafeRestoreZone(saveTheZone);
	A5SimpleRestore(olda5);

	return err;
	}

pascal OSErr
C_PostText(Ptr textPtr,short len)
	{
	FakeKeyPtr		pFakeKey;
	unsigned short	index;
	unsigned char	*pChar;
	unsigned long	olda5;
	OSErr			err;
	THz				saveTheZone;

	olda5 = ProcessMgrA5SimpleSetup();
	SafeSetZone(saveTheZone, ProcessMgrZone);

	/* Allocate memory for this event */
	SetHandleSize( (Handle)fakeKeyHandle, sizeof(FakeKeyRecord)*(fakeKeyCount+len));
	if ( (err = MEMERROR) == noErr )
		{
		pFakeKey = *fakeKeyHandle + fakeKeyCount;
		pChar	 = textPtr;
		for ( index=len; index>0; index--, pFakeKey++ )
			{
			pFakeKey->message	= *pChar++;
			pFakeKey->modifiers	= 0;
			}
		fakeKeyCount += len;
		}
	WakeUpProcess(&pFrontProcess->p_serialNumber);
	SafeRestoreZone(saveTheZone);
	A5SimpleRestore(olda5);

	return err;
	}

Boolean
OSEventAvailForFront(short eventmask, EventRecord *theEvent)
	{
	return OSEventAvail(eventmask, theEvent) /*&& ! imLayerKeyDown(theEvent, false)*/;
	}

/************************************************************************************
 *								 Supporting patches.
 ************************************************************************************/

/* c_stilldown.  For puppeteering, force a true.  In general, disallow minor and major
 * switches while checking the mouse (_StillDown makes a toolbox event call).
 */
pascal short
c_stilldown(void)
	{
	unsigned long 	olda5;
	short			retval;
	
	olda5 = ProcessMgrA5Setup();
	if (PuppetForceButtonDown())
		retval = wTrue;
	else
		{
		/* Otherwise just pass it on to the old routine */
		DisableSwitching();
		retval = CALL_FNC_PTR(Stilldown_ptr,patchtraps[STILLDOWN].oldtrap,());
		EnableSwitching();
		}
		
	A5Restore(olda5);
	return retval;
	}
	
/* c_waitmouseup. Wait for the mouse to come up, but don't allow switching (_WaitMouseUp
 * makes a toolbox event call).
 */
pascal short
c_waitmouseup(void)
	{
	pascal short	(*oldtrap)();
	unsigned long	olda5;
	short			retval;

	olda5 = ProcessMgrA5SimpleSetup();
	oldtrap = patchtraps[WAITMOUSEUP].oldtrap;
	A5SimpleRestore(olda5);
	
	DisableSwitching();
	retval = (*oldtrap)();
	EnableSwitching();

	return(retval);
	}
