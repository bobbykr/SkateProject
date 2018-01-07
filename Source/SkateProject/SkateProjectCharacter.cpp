// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "SkateProjectCharacter.h"
#include "SkateController.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"

//////////////////////////////////////////////////////////////////////////
// ASkateProjectCharacter

ASkateProjectCharacter::ASkateProjectCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	MinimumRequiredDot = 0.6f;
	TrickPointExecDistance = 0.2f;
	Deadzone = 0.05f;
	LastTrickLocation = FVector2D(0.f, 0.f);
	PreviousLocation = FVector2D(0.1f, 0.f);

	bIsDragging = false;

}

void ASkateProjectCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void ASkateProjectCharacter::CustomBeginPlay()
{
	SkatePC = Cast<ASkateController>(GetController());
	if (!SkatePC)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("Player Character: NO PC FOUND"));
	}
	//Calculate Joystick Position for each trickpoint
	for (int i = 0; i < Tricks.Num(); i++)
	{
		for (int j = 0; j < Tricks[i].Points.Num(); j++)
		{
			Tricks[i].Points[j].DesiredPosition = FVector2D(FMath::Cos(FMath::DegreesToRadians(Tricks[i].Points[j].Angle)), FMath::Sin(FMath::DegreesToRadians(Tricks[i].Points[j].Angle)));
		}
	}
	TrickQueue.Empty();
	TrickQueue.Append(Tricks);
	if (SkatePC)
	{
		SkatePC->UpdateLastLocation(LastTrickLocation);
	}
}

void ASkateProjectCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	GEngine->AddOnScreenDebugMessage(1, .2f, FColor::Blue, FString::Printf(TEXT("%s"), *AnalogRaw.ToString()));
	GEngine->AddOnScreenDebugMessage(2, 2.f, FColor::Blue, FString::Printf(TEXT("%d"), bIsDragging));

	FVector2D AnalogLocation = AnalogRaw;
	AnalogLocation.Normalize();

	//Update analog location in UI
	if (SkatePC)
	{
		SkatePC->UpdateAnalogLocation(AnalogRaw);
	}

	//After the player executed a trick, the analog must return to the deadzone
	if (bReturningFromTrick)
	{
		if (AnalogRaw.Size() > Deadzone)
		{
			return;
		}
		bReturningFromTrick = false;
	}

	//Check if the player is still dragging the analog along the edge
	if (bIsDragging && AnalogRaw.Size() < 0.9f) bIsDragging = false;

	for (int i = 0; i < TrickQueue.Num(); i++)
	{
		//Only check for angles between trickpoints if the player already reached a trick point
		if (TrickPointIndex != 0)
		{
			FVector2D Vector1 = AnalogRaw - LastTrickLocation;
			FVector2D Vector2 = TrickQueue[i].Points[TrickPointIndex].DesiredPosition - LastTrickLocation;
			Vector1.Normalize();
			Vector2.Normalize();
			//Check angle between analog direction and trickpoint direction
			float Dot = FVector2D::DotProduct(Vector1, Vector2);

			/*
			If this trick requires drag and player isn't dragging
			or the joystick is not going in the direction of this trick,
			remove it from the queue
			*/
			if ((TrickQueue[i].Points[TrickPointIndex].bDrag && !bIsDragging) || Dot < MinimumRequiredDot)
			{
				/*If the analog is still really close to the last trick location, don't remove trick,
				give the player a chance*/
				if ((AnalogRaw - LastTrickLocation).Size() >= TrickPointExecDistance*2.f)
				{
					GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Blue, FString::Printf(TEXT("Removed %s at %f"), *TrickQueue[i].TrickName.ToString(), Dot));
					RemoveTrick(i);
					break;
				}
			}
		}

		//Check if the analog has reached a TrickPoint location
		if ((AnalogRaw - TrickQueue[i].Points[TrickPointIndex].DesiredPosition).Size() <= TrickPointExecDistance)
		{
			TrickPointIndex++;
			LastTrickLocation = TrickQueue[i].Points[TrickPointIndex-1].DesiredPosition;

			//If this trick hit its last trick point, select as eligible trick
			if (TrickPointIndex == TrickQueue[i].Points.Num())
			{
				EligibleTrick = TrickQueue[i];
				RemoveTrick(i);

				/*Remove all the other tricks with the same amount of trickpoints from the queue,
				the trick and index i is the eligible trick!*/
				int TricksRemoved = 0;
				for (int j = 0; j < (TrickQueue.Num() + TricksRemoved); ++j)
				{
					if (TrickQueue[j - TricksRemoved].Points.Num() == TrickPointIndex)
					{
						RemoveTrick(j - TricksRemoved);
						TricksRemoved++;
					}
				}
			}
			//If this trick doesn't have sufficient trick points anymore, remove from queue
			else if (TrickPointIndex > TrickQueue[i].Points.Num())
			{
				GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Blue, TEXT("IS THIS EVEN BEING CALLED??????"));
				//RemoveTrick(i);
			}

			if (SkatePC)
			{
				SkatePC->UpdateTrickLocation(TrickQueue[0].Points[1].DesiredPosition);
			}
			break;
		}
	}

	FVector2D Dir = AnalogRaw - PreviousLocation;
	PreviousLocation = AnalogRaw;
}

void ASkateProjectCharacter::AttemptExecuteTrick()
{
	//Check if the eligible trick is valid
	if (EligibleTrick.Points.Num() != 0)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Blue, FString::Printf(TEXT("%s"), *EligibleTrick.TrickName.ToString()));
	}
	TrickPointIndex = 0;
	LastTrickLocation = FVector2D(0.f, 0.f);
	bIsDragging = true;
	bReturningFromTrick = true;
	TrickQueue.Empty();
	TrickQueue.Append(Tricks);
	EligibleTrick.Points.Empty();
}

void ASkateProjectCharacter::RemoveTrick(int index)
{
	TrickQueue.RemoveAt(index);
	//If there are no more eligible tricks, attempt to execute the eligible trick
	if (TrickQueue.Num() == 0) AttemptExecuteTrick();
}

//////////////////////////////////////////////////////////////////////////
// Input

void ASkateProjectCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &ASkateProjectCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ASkateProjectCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ASkateProjectCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ASkateProjectCharacter::LookUpAtRate);
}

void ASkateProjectCharacter::TurnAtRate(float Rate)
{
	AnalogRaw.X = Rate;
}

void ASkateProjectCharacter::LookUpAtRate(float Rate)
{
	AnalogRaw.Y = -Rate;
}

void ASkateProjectCharacter::MoveForward(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void ASkateProjectCharacter::MoveRight(float Value)
{
	if ( (Controller != NULL) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}
