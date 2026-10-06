export function passwordsMatch(password: string, confirmPassword: string): boolean {
    return password === confirmPassword;
}

export function createPasswordMatchValidator() {
    return (formGroup: any) => {
        const password = formGroup.get('password');
        const confirmPassword = formGroup.get('confirmPassword');

        if (!password || !confirmPassword) {
            return null;
        }

        return password.value === confirmPassword.value ? null : { passwordMismatch: true };
    };
}