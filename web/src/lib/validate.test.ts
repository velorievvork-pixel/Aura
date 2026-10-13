import { describe, expect, it } from 'vitest';
import { cleanCode, emailError, nameError, otpError, passwordError, twoFaCodeError } from './validate';

describe('validate', () => {
  it('email', () => {
    expect(emailError('')).not.toBeNull();
    expect(emailError('alex@aura')).not.toBeNull();
    expect(emailError(' alex@aura.local ')).toBeNull();
  });
  it('пароль: от 8 символов, буквы и цифры (как на сервере)', () => {
    expect(passwordError('a1b2c3')).toMatch(/8/);
    expect(passwordError('abcdefgh')).toMatch(/цифры/);
    expect(passwordError('12345678')).toMatch(/буквы/);
    expect(passwordError('пароль2026')).toBeNull();
    expect(passwordError('Secret123')).toBeNull();
  });
  it('имя', () => {
    expect(nameError('  ')).not.toBeNull();
    expect(nameError('я'.repeat(81))).not.toBeNull();
    expect(nameError('Алекс')).toBeNull();
  });
  it('код из письма: только цифры, не больше 6', () => {
    expect(cleanCode('123 456')).toBe('123456');
    expect(cleanCode('12-34-56-78')).toBe('123456');
    expect(otpError('12345')).not.toBeNull();
    expect(otpError('123456')).toBeNull();
  });
  it('2FA: TOTP или резервный код', () => {
    expect(twoFaCodeError('123456')).toBeNull();
    expect(twoFaCodeError('abcde-fgh23')).toBeNull();
    expect(twoFaCodeError('12345')).not.toBeNull();
    expect(twoFaCodeError('ABCDEFGHJK')).not.toBeNull();
  });
});
