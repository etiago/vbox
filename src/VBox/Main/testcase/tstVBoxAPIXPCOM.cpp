/** @file
 *
 * tstVBoxAPILinux - sample program to illustrate the VirtualBox
 *                   XPCOM API for machine management on Linux.
 *                   It only uses standard C/C++ and XPCOM semantics,
 *                   no additional VBox classes/macros/helpers.
 */

/*
 * Copyright (C) 2006-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*
 * PURPOSE OF THIS SAMPLE PROGRAM
 * ------------------------------
 *
 * This sample program is intended to demonstrate the minimal code necessary
 * to use VirtualBox XPCOM API for learning puroses only. The program uses
 * pure XPCOM and doesn't have any extra dependencies to let you better
 * understand what is going on when a client talks to the VirtualBox core
 * using the XPCOM framework.
 *
 * However, if you want to write a real application, it is highly recommended
 * to use our MS COM XPCOM Glue library and helper C++ classes. This way, you
 * will get at least the following benefits:
 *
 * a) better portability: both the MS COM (used on Windows) and XPCOM (used
 *    everywhere else) VirtualBox client application from the same source code
 *    (including common smart C++ templates for automatic interface pointer
 *    reference counter and string data management);
 * b) simpler XPCOM initialization and shutdown (only a single method call
 *    that does everything right).
 *
 * Currently, there is no separate sample program that uses the VirtualBox MS
 * COM XPCOM Glue library. Please refer to the sources of stock VirtualBox
 * applications such as the VirtualBox GUI frontend or the VBoxManage command
 * line frontend.
 *
 *
 * RUNNING THIS SAMPLE PROGRAM
 * ---------------------------
 *
 * This sample program needs to know where the VirtualBox core files reside
 * and where to search for VirtualBox shared libraries. Therefore, you need to
 * use the following (or similar) command to execute it:
 *
 *   $ env VBOX_XPCOM_HOME=../../.. LD_LIBRARY_PATH=../../.. ./tstVBoxAPILinux
 *
 * The above command assumes that VBoxRT.so, VBoxXPCOM.so and others reside in
 * the directory ../../..
 */


#include <stdio.h>
#include <stdlib.h>
#include <iconv.h>

/*
 * Include the XPCOM headers
 */
#include <nsMemory.h>
#include <nsString.h>
#include <nsIServiceManager.h>
#include <nsEventQueueUtils.h>

#include <nsIExceptionService.h>

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/EventQueue.h>

#include <VBox/com/VirtualBox.h>

#include <iprt/stream.h>


/*
 * VirtualBox XPCOM interface. This header is generated
 * from IDL which in turn is generated from a custom XML format.
 */
#include "VirtualBox_XPCOM.h"

/*
 * Prototypes
 */

char *nsIDToString(nsID *guid);
void printErrorInfo();


/**
 * Display all registered VMs on the screen with some information about each
 *
 * @param virtualBox VirtualBox instance object.
 */
void listVMs(IVirtualBox *virtualBox)
{
    nsresult rc;

    RTPrintf("----------------------------------------------------\n");
    RTPrintf("VM List:\n\n");

    /*
     * Get the list of all registered VMs
     */
    IMachine **machines = NULL;
    PRUint32 machineCnt = 0;

    rc = virtualBox->GetMachines(&machineCnt, &machines);
    if (NS_SUCCEEDED(rc))
    {
        /*
         * Iterate through the collection
         */
        for (PRUint32 i = 0; i < machineCnt; ++ i)
        {
            IMachine *machine = machines[i];
            if (machine)
            {
                PRBool isAccessible = PR_FALSE;
                machine->GetAccessible(&isAccessible);

                if (isAccessible)
                {
                    nsXPIDLString machineName;
                    machine->GetName(getter_Copies(machineName));
                    char *machineNameAscii = ToNewCString(machineName);
                    RTPrintf("\tName:        %s\n", machineNameAscii);
                    free(machineNameAscii);
                }
                else
                {
                    RTPrintf("\tName:        <inaccessible>\n");
                }

                nsXPIDLString iid;
                machine->GetId(getter_Copies(iid));
                const char *uuidString = ToNewCString(iid);
                RTPrintf("\tUUID:        %s\n", uuidString);
                free((void*)uuidString);

                if (isAccessible)
                {
                    nsXPIDLString configFile;
                    machine->GetSettingsFilePath(getter_Copies(configFile));
                    char *configFileAscii = ToNewCString(configFile);
                    RTPrintf("\tConfig file: %s\n", configFileAscii);
                    free(configFileAscii);

                    PRUint32 memorySize;
                    machine->GetMemorySize(&memorySize);
                    RTPrintf("\tMemory size: %uMB\n", memorySize);

                    nsXPIDLString typeId;
                    machine->GetOSTypeId(getter_Copies(typeId));
                    IGuestOSType *osType = nsnull;
                    virtualBox->GetGuestOSType (typeId.get(), &osType);
                    nsXPIDLString osName;
                    osType->GetDescription(getter_Copies(osName));
                    char *osNameAscii = ToNewCString(osName);
                    RTPrintf("\tGuest OS:    %s\n\n", osNameAscii);
                    free(osNameAscii);
                    osType->Release();
                }

                /* don't forget to release the objects in the array... */
                machine->Release();
            }
        }
    }
    RTPrintf("----------------------------------------------------\n\n");
}

/**
 * Create a sample VM
 *
 * @param virtualBox VirtualBox instance object.
 */
void createVM(IVirtualBox *virtualBox)
{
    nsresult rc;
    /*
     * First create a unnamed new VM. It will be unconfigured and not be saved
     * in the configuration until we explicitely choose to do so.
     */
    nsCOMPtr<IMachine> machine;
    rc = virtualBox->CreateMachine(NULL,        /* settings file */
                                   NS_LITERAL_STRING("A brand new name").get(),
                                   0, nsnull,   /* groups (safearray)*/
                                   nsnull,      /* ostype */
                                   nsnull,      /* create flags */
                                   getter_AddRefs(machine));
    if (NS_FAILED(rc))
    {
        RTPrintf("Error: could not create machine! rc=%Rhrc\n", rc);
        return;
    }

    /*
     * Set some properties
     */
    /* alternative to illustrate the use of string classes */
    rc = machine->SetName(NS_ConvertUTF8toUTF16("A new name").get());
    rc = machine->SetMemorySize(128);

    /*
     * Now a more advanced property -- the guest OS type. This is
     * an object by itself which has to be found first. Note that we
     * use the ID of the guest OS type here which is an internal
     * representation (you can find that by configuring the OS type of
     * a machine in the GUI and then looking at the <Guest ostype=""/>
     * setting in the XML file. It is also possible to get the OS type from
     * its description (win2k would be "Windows 2000") by getting the
     * guest OS type collection and enumerating it.
     */
    nsCOMPtr<IGuestOSType> osType;
    rc = virtualBox->GetGuestOSType(NS_LITERAL_STRING("Windows2000").get(),
                                    getter_AddRefs(osType));
    if (NS_FAILED(rc))
    {
        RTPrintf("Error: could not find guest OS type! rc=%Rhrc\n", rc);
    }
    else
    {
        machine->SetOSTypeId (NS_LITERAL_STRING("Windows2000").get());
    }

    /*
     * Register the VM. Note that this call also saves the VM config
     * to disk. It is also possible to save the VM settings but not
     * register the VM.
     *
     * Also note that due to current VirtualBox limitations, the machine
     * must be registered *before* we can attach hard disks to it.
     */
    rc = virtualBox->RegisterMachine(machine);
    if (NS_FAILED(rc))
    {
        RTPrintf("Error: could not register machine! rc=%Rhrc\n", rc);
        printErrorInfo();
        return;
    }

    nsCOMPtr<IMachine> origMachine = machine;

    /*
     * In order to manipulate the registered machine, we must open a session
     * for that machine. Do it now.
     */
    nsCOMPtr<ISession> session;
    nsCOMPtr<IMachine> sessionMachine;
    {
        nsCOMPtr<nsIComponentManager> manager;
        rc = NS_GetComponentManager (getter_AddRefs (manager));
        if (NS_FAILED(rc))
        {
            RTPrintf("Error: could not get component manager! rc=%Rhrc\n", rc);
            return;
        }
        rc = manager->CreateInstanceByContractID (NS_SESSION_CONTRACTID,
                                                  nsnull,
                                                  NS_GET_IID(ISession),
                                                  getter_AddRefs(session));
        if (NS_FAILED(rc))
        {
            RTPrintf("Error, could not instantiate session object! rc=%Rhrc\n", rc);
            return;
        }

        rc = machine->LockMachine(session, LockType_Write);
        if (NS_FAILED(rc))
        {
            RTPrintf("Error, could not lock the machine for the session! rc=%Rhrc\n", rc);
            return;
        }

        /*
         * After the machine is registered, the initial machine object becomes
         * immutable. In order to get a mutable machine object, we must query
         * it from the opened session object.
         */
        rc = session->GetMachine(getter_AddRefs(sessionMachine));
        if (NS_FAILED(rc))
        {
            RTPrintf("Error, could not get machine session! rc=%Rhrc\n", rc);
            return;
        }
    }

    /*
     * Create a virtual harddisk
     */
    nsCOMPtr<IMedium> hardDisk = 0;
    rc = virtualBox->CreateHardDisk(NS_LITERAL_STRING("VDI").get(),
                                    NS_LITERAL_STRING("TestHardDisk.vdi").get(),
                                    getter_AddRefs(hardDisk));
    if (NS_FAILED(rc))
    {
        RTPrintf("Failed creating a hard disk object! rc=%Rhrc\n", rc);
    }
    else
    {
        /*
         * We have only created an object so far. No on disk representation exists
         * because none of its properties has been set so far. Let's continue creating
         * a dynamically expanding image.
         */
        nsCOMPtr <IProgress> progress;
        com::SafeArray<MediumVariant_T> mediumVariant;
        mediumVariant.push_back(MediumVariant_Standard);
        rc = hardDisk->CreateBaseStorage(100,                                // size in megabytes
                                         ComSafeArrayAsInParam(mediumVariant),
                                         getter_AddRefs(progress));          // optional progress object
        if (NS_FAILED(rc))
        {
            RTPrintf("Failed creating hard disk image! rc=%Rhrc\n", rc);
        }
        else
        {
            /*
             * Creating the image is done in the background because it can take quite
             * some time (at least fixed size images). We have to wait for its completion.
             * Here we wait forever (timeout -1)  which is potentially dangerous.
             */
            rc = progress->WaitForCompletion(-1);
            PRInt32 resultCode;
            progress->GetResultCode(&resultCode);
            if (NS_FAILED(rc) || NS_FAILED(resultCode))
            {
                RTPrintf("Error: could not create hard disk! rc=%Rhrc\n",
                       NS_FAILED(rc) ? rc : resultCode);
            }
            else
            {
                /*
                 * Now that it's created, we can assign it to the VM.
                 */
                rc = sessionMachine->AttachDevice(
                                           NS_LITERAL_STRING("IDE Controller").get(), // controller identifier
                                           0,                              // channel number on the controller
                                           0,                              // device number on the controller
                                           DeviceType_HardDisk,
                                           hardDisk);
                if (NS_FAILED(rc))
                {
                    RTPrintf("Error: could not attach hard disk! rc=%Rhrc\n", rc);
                }
            }
        }
    }

    /*
     * It's got a hard disk but that one is new and thus not bootable. Make it
     * boot from an ISO file. This requires some processing. First the ISO file
     * has to be registered and then mounted to the VM's DVD drive and selected
     * as the boot device.
     */
    nsCOMPtr<IMedium> dvdImage;
    rc = virtualBox->OpenMedium(NS_LITERAL_STRING("/home/vbox/isos/winnt4ger.iso").get(),
                                DeviceType_DVD,
                                AccessMode_ReadOnly,
                                false /* fForceNewUuid */,
                                getter_AddRefs(dvdImage));
    if (NS_FAILED(rc))
        RTPrintf("Error: could not open CD image! rc=%Rhrc\n", rc);
    else
    {
        /*
         * Now assign it to our VM
         */
        rc = sessionMachine->MountMedium(
                                  NS_LITERAL_STRING("IDE Controller").get(), // controller identifier
                                  2,                              // channel number on the controller
                                  0,                              // device number on the controller
                                  dvdImage,
                                  PR_FALSE);                      // aForce
        if (NS_FAILED(rc))
        {
            RTPrintf("Error: could not mount ISO image! rc=%Rhrc\n", rc);
        }
        else
        {
            /*
             * Last step: tell the VM to boot from the CD.
             */
            rc = sessionMachine->SetBootOrder (1, DeviceType::DVD);
            if (NS_FAILED(rc))
            {
                RTPrintf("Could not set boot device! rc=%Rhrc\n", rc);
            }
        }
    }

    /*
     * Save all changes we've just made.
     */
    rc = sessionMachine->SaveSettings();
    if (NS_FAILED(rc))
        RTPrintf("Could not save machine settings! rc=%Rhrc\n", rc);

    /*
     * It is always important to close the open session when it becomes not
     * necessary any more.
     */
    session->UnlockMachine();

    com::SafeIfaceArray<IMedium> aMedia;
    rc = machine->Unregister((CleanupMode_T)CleanupMode_DetachAllReturnHardDisksOnly,
                             ComSafeArrayAsOutParam(aMedia));
    if (NS_FAILED(rc))
        RTPrintf("Unregistering the machine failed! rc=%Rhrc\n", rc);
    else
    {
        ComPtr<IProgress> pProgress;
        rc = machine->DeleteConfig(ComSafeArrayAsInParam(aMedia), pProgress.asOutParam());
        if (NS_FAILED(rc))
            RTPrintf("Deleting of machine failed! rc=%Rhrc\n", rc);
        else
        {
            rc = pProgress->WaitForCompletion(-1);
            PRInt32 resultCode;
            pProgress->GetResultCode(&resultCode);
            if (NS_FAILED(rc) || NS_FAILED(resultCode))
                RTPrintf("Failed to delete the machine! rc=%Rhrc\n",
                         NS_FAILED(rc) ? rc : resultCode);
        }
    }
}

// main
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    /*
     * Check that PRUnichar is equal in size to what compiler composes L""
     * strings from; otherwise NS_LITERAL_STRING macros won't work correctly
     * and we will get a meaningless SIGSEGV. This, of course, must be checked
     * at compile time in xpcom/string/nsTDependentString.h, but XPCOM lacks
     * compile-time assert macros and I'm not going to add them now.
     */
    if (sizeof(PRUnichar) != sizeof(wchar_t))
    {
        RTPrintf("Error: sizeof(PRUnichar) {%lu} != sizeof(wchar_t) {%lu}!\n"
               "Probably, you forgot the -fshort-wchar compiler option.\n",
               (unsigned long) sizeof(PRUnichar),
               (unsigned long) sizeof(wchar_t));
        return -1;
    }

    nsresult rc;

    /*
     * This is the standard XPCOM init procedure.
     * What we do is just follow the required steps to get an instance
     * of our main interface, which is IVirtualBox.
     *
     * Note that we scope all nsCOMPtr variables in order to have all XPCOM
     * objects automatically released before we call NS_ShutdownXPCOM at the
     * end. This is an XPCOM requirement.
     */
    {
        nsCOMPtr<nsIServiceManager> serviceManager;
        rc = NS_InitXPCOM2(getter_AddRefs(serviceManager), nsnull, nsnull);
        if (NS_FAILED(rc))
        {
            RTPrintf("Error: XPCOM could not be initialized! rc=%Rhrc\n", rc);
            return -1;
        }

#if 0
        /*
         * Register our components. This step is only necessary if this executable
         * implements XPCOM components itself which is not the case for this
         * simple example.
         */
        nsCOMPtr<nsIComponentRegistrar> registrar = do_QueryInterface(serviceManager);
        if (!registrar)
        {
            RTPrintf("Error: could not query nsIComponentRegistrar interface!\n");
            return -1;
        }
        registrar->AutoRegister(nsnull);
#endif

        /*
         * Make sure the main event queue is created. This event queue is
         * responsible for dispatching incoming XPCOM IPC messages. The main
         * thread should run this event queue's loop during lengthy non-XPCOM
         * operations to ensure messages from the VirtualBox server and other
         * XPCOM IPC clients are processed. This use case doesn't perform such
         * operations so it doesn't run the event loop.
         */
        nsCOMPtr<nsIEventQueue> eventQ;
        rc = NS_GetMainEventQ(getter_AddRefs (eventQ));
        if (NS_FAILED(rc))
        {
            RTPrintf("Error: could not get main event queue! rc=%Rhrc\n", rc);
            return -1;
        }

        /*
         * Now XPCOM is ready and we can start to do real work.
         * IVirtualBox is the root interface of VirtualBox and will be
         * retrieved from the XPCOM component manager. We use the
         * XPCOM provided smart pointer nsCOMPtr for all objects because
         * that's very convenient and removes the need deal with reference
         * counting and freeing.
         */
        nsCOMPtr<nsIComponentManager> manager;
        rc = NS_GetComponentManager (getter_AddRefs (manager));
        if (NS_FAILED(rc))
        {
            RTPrintf("Error: could not get component manager! rc=%Rhrc\n", rc);
            return -1;
        }

        nsCOMPtr<IVirtualBox> virtualBox;
        rc = manager->CreateInstanceByContractID (NS_VIRTUALBOX_CONTRACTID,
                                                  nsnull,
                                                  NS_GET_IID(IVirtualBox),
                                                  getter_AddRefs(virtualBox));
        if (NS_FAILED(rc))
        {
            RTPrintf("Error, could not instantiate VirtualBox object! rc=%Rhrc\n", rc);
            return -1;
        }
        RTPrintf("VirtualBox object created\n");

        ////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////


        listVMs(virtualBox);

        createVM(virtualBox);


        ////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////

        /* this is enough to free the IVirtualBox instance -- smart pointers rule! */
        virtualBox = nsnull;

        /*
         * Process events that might have queued up in the XPCOM event
         * queue. If we don't process them, the server might hang.
         */
        eventQ->ProcessPendingEvents();
    }

    /*
     * Perform the standard XPCOM shutdown procedure.
     */
    NS_ShutdownXPCOM(nsnull);
    RTPrintf("Done!\n");
    return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
//// Helpers
//////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Helper function to convert an nsID into a human readable string
 *
 * @returns result string, allocated. Has to be freed using free()
 * @param   guid Pointer to nsID that will be converted.
 */
char *nsIDToString(nsID *guid)
{
    char *res = (char*)malloc(39);

    if (res != NULL)
    {
        RTStrPrintf(res, 39, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                 guid->m0, (PRUint32)guid->m1, (PRUint32)guid->m2,
                 (PRUint32)guid->m3[0], (PRUint32)guid->m3[1], (PRUint32)guid->m3[2],
                 (PRUint32)guid->m3[3], (PRUint32)guid->m3[4], (PRUint32)guid->m3[5],
                 (PRUint32)guid->m3[6], (PRUint32)guid->m3[7]);
    }
    return res;
}

/**
 * Helper function to print XPCOM exception information set on the current
 * thread after a failed XPCOM method call. This function will also print
 * extended VirtualBox error info if it is available.
 */
void printErrorInfo()
{
    nsresult rc;

    nsCOMPtr <nsIExceptionService> es;
    es = do_GetService (NS_EXCEPTIONSERVICE_CONTRACTID, &rc);
    if (NS_SUCCEEDED(rc))
    {
        nsCOMPtr <nsIExceptionManager> em;
        rc = es->GetCurrentExceptionManager (getter_AddRefs (em));
        if (NS_SUCCEEDED(rc))
        {
            nsCOMPtr<nsIException> ex;
            rc = em->GetCurrentException (getter_AddRefs (ex));
            if (NS_SUCCEEDED(rc) && ex)
            {
                nsCOMPtr <IVirtualBoxErrorInfo> info;
                info = do_QueryInterface(ex, &rc);
                if (NS_SUCCEEDED(rc) && info)
                {
                    /* got extended error info */
                    RTPrintf ("Extended error info (IVirtualBoxErrorInfo):\n");
                    PRInt32 resultCode = NS_OK;
                    info->GetResultCode (&resultCode);
                    RTPrintf ("  resultCode=%08X\n", resultCode);
                    nsXPIDLString component;
                    info->GetComponent (getter_Copies (component));
                    RTPrintf ("  component=%s\n", NS_ConvertUTF16toUTF8(component).get());
                    nsXPIDLString text;
                    info->GetText (getter_Copies (text));
                    RTPrintf ("  text=%s\n", NS_ConvertUTF16toUTF8(text).get());
                }
                else
                {
                    /* got basic error info */
                    RTPrintf ("Basic error info (nsIException):\n");
                    nsresult resultCode = NS_OK;
                    ex->GetResult (&resultCode);
                    RTPrintf ("  resultCode=%08X\n", resultCode);
                    nsXPIDLCString message;
                    ex->GetMessage (getter_Copies (message));
                    RTPrintf ("  message=%s\n", message.get());
                }

                /* reset the exception to NULL to indicate we've processed it */
                em->SetCurrentException (NULL);

                rc = NS_OK;
            }
        }
    }
}
