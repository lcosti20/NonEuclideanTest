#include "NEPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "NEGameMode.h"
#include "NEVideoLook.h"
#include "PortalGrabComponent.h"
#include "PortalTraversalComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Original engine constants (GameHeader.h), converted to cm.
	constexpr float PlayerRadius = 20.0f;      // GH_PLAYER_RADIUS 0.2 m
	constexpr float PlayerHalfHeight = 85.0f;  // feet 1.5 m below the eye, head sphere 0.2 m above it
	constexpr float WalkSpeed = 290.0f;        // GH_WALK_SPEED 2.9 m/s
	constexpr float WalkAccel = 5000.0f;       // GH_WALK_ACCEL 50 m/s^2
	constexpr float VerticalFOV = 60.0f;       // GH_FOV (vertical)

	// Head bob (GH_BOB_*). The original steps at 500 Hz; these are the per-second equivalents.
	constexpr float BobFreq = 8.0f;            // rad/s
	constexpr float BobOffset = 0.015f;        // m of lift per m/s of speed
	constexpr float BobMin = 0.1f;             // m/s
	constexpr float BobDampPerSecond = 20.4f;  // (1 - 0.04)^500 == e^-20.4
}

ANEPlayerCharacter::ANEPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(PlayerRadius, PlayerHalfHeight);
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BaseEyeHeight = EyeOffset;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, EyeOffset));
	Camera->bUsePawnControlRotation = true;
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
	// With MaintainYFOV, FieldOfView is the horizontal FOV at the camera's AspectRatio, and the
	// vertical FOV derived from it is kept for any window shape.
	Camera->AspectRatio = 16.0f / 9.0f;
	Camera->FieldOfView = FMath::RadiansToDegrees(
		2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFOV * 0.5f)) * Camera->AspectRatio));
	Camera->PostProcessBlendWeight = 1.0f;
	NEVideoLook::Apply(Camera->PostProcessSettings);

	PortalTraversal = CreateDefaultSubobject<UPortalTraversalComponent>(TEXT("PortalTraversal"));
	PortalTraversal->Eye = Camera;

	PortalGrab = CreateDefaultSubobject<UPortalGrabComponent>(TEXT("PortalGrab"));

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = WalkAccel;
	Move->BrakingDecelerationWalking = WalkAccel;
	Move->GroundFriction = 8.0f;
	Move->bOrientRotationToMovement = false;
	bUseControllerRotationYaw = true;

	static ConstructorHelpers::FObjectFinder<UInputAction> MoveFinder(TEXT("/Game/Input/Actions/IA_Move"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookFinder(TEXT("/Game/Input/Actions/IA_Look"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookFinder(TEXT("/Game/Input/Actions/IA_MouseLook"));
	MoveAction = MoveFinder.Object;
	LookAction = LookFinder.Object;
	MouseLookAction = MouseLookFinder.Object;
}

void ANEPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// No jump binding: jumping is compiled out in the original (Player.cpp, "#if 0").
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ANEPlayerCharacter::MoveInput);
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ANEPlayerCharacter::LookInput);
		Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ANEPlayerCharacter::LookInput);
	}

	// Keys 1-8 switch demos, like the original's level keys (7, the mirror tunnel, and 8, the object puzzles, are new).
	static const FKey DemoKeys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(DemoKeys); ++Index)
	{
		FInputKeyBinding Binding(FInputChord(DemoKeys[Index]), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Index]()
		{
			ANEGameMode::OpenDemo(this, Index);
		});
		PlayerInputComponent->KeyBindings.Add(Binding);
	}

	// Objects: E picks up / drops, left click throws.
	FInputKeyBinding GrabBinding(FInputChord(EKeys::E), IE_Pressed);
	GrabBinding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { PortalGrab->ToggleGrab(); });
	PlayerInputComponent->KeyBindings.Add(GrabBinding);
	FInputKeyBinding ThrowBinding(FInputChord(EKeys::LeftMouseButton), IE_Pressed);
	ThrowBinding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { PortalGrab->Throw(); });
	PlayerInputComponent->KeyBindings.Add(ThrowBinding);
}

void ANEPlayerCharacter::MoveInput(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddMovementInput(GetActorRightVector(), Axis.X);
	AddMovementInput(GetActorForwardVector(), Axis.Y);
}

void ANEPlayerCharacter::LookInput(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ANEPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	float LiftCm = 0.0f;
	if (bHeadBob)
	{
		// Speed in m/s relative to the player's own size, zero while airborne (as in the original).
		const float Scale = GetActorScale3D().Z;
		const bool bOnGround = GetCharacterMovement()->IsMovingOnGround();
		const float Speed = bOnGround ? GetVelocity().Size2D() / (100.0f * Scale) : 0.0f;

		const float Alpha = 1.0f - FMath::Exp(-BobDampPerSecond * DeltaSeconds);
		BobMagnitude = FMath::Lerp(BobMagnitude, Speed, Alpha);

		if (BobMagnitude < BobMin)
		{
			BobPhase = 0.0f;
		}
		else
		{
			BobPhase = FMath::Fmod(BobPhase + BobFreq * DeltaSeconds, 2.0f * PI);
			const float Theta = HALF_PI * FMath::Sin(BobPhase);
			LiftCm = BobMagnitude * BobOffset * (1.0f - FMath::Cos(Theta)) * 100.0f;
		}
	}

	// The camera is a child of the capsule, so this offset scales with the actor.
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, EyeOffset + LiftCm));
}
